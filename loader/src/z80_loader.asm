;;
;; Module z80_loader:
;;
;; Accepts a stream of bytes, unpacks it as a .z80 snapshot,
;; loads it into RAM, and executes it.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
;;
;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use,
;; copy, modify, merge, publish, distribute, sublicense, and/or sell
;; copies of the Software, and to permit persons to whom the
;; Software is furnished to do so, subject to the following
;; conditions:
;;
;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.
;;
;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;; OTHER DEALINGS IN THE SOFTWARE.

    .module z80_loader

    .include "z80_loader.inc"

    .include "context_switch.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================

Z80_ESCAPE         = 0xED     ;; escape byte in compressed chunks

PROGRESS_BAR_BASE  = ATTRS_BASE + 0x2E0

;; ----------------------------------------------------------------------------

    .area _DATA

_repcount:
    .ds   1         ;; repetition count for ED ED sequences

;; ----------------------------------------------------------------------------
;; expected and currently loaded no. of kilobytes, for progress display
;; ----------------------------------------------------------------------------

kilobytes_expected:
    .ds   1
kilobytes_loaded:
    .ds   1

;; ----------------------------------------------------------------------------
;; digits (BCD) for progress display while loading a snapshot
;; ----------------------------------------------------------------------------

_digits:
    .ds   1

;; ============================================================================

;; ----------------------------------------------------------------------------
;; The Z80 snapshot state machine is implemented by one routine for each
;; state. The function returns whenever one of the following happens:
;;
;; - all currently available data has been consumed (HL == 0)
;; - a state transition is required
;; - the DE write pointer has reached an integral number of kilobytes
;; ----------------------------------------------------------------------------
;;
;; States:
;;  
;;                        HEADER
;;                           |
;; (for v.1 snapshots) /-----+-----\ (for v.2+ snapshots)
;;                     |           |
;;                     |           v  
;;                     |      CHUNK_HEADER <-----------------------\
;;                     |           |                               |
;;                     v           v                               |
;;                     |      CHUNK_HEADER2                        |
;;                     |           |                               |
;;                     |           v                               ^
;;                     |      CHUNK_HEADER3                        |
;;                     |           |                               |
;;                     \--v--------/                               |
;;                        |                                        |
;;                        |                                        |
;;                        +---> CHUNK_WRITE_DATA_UNCOMPRESSED -->--+
;;                        |                                        |
;;                        v                                        |
;;          /-------> CHUNK_WRITE_DATA_COMPRESSED --------->-------/ 
;;          |            |        ^
;;          |            v        |
;;          ^      CHUNK_COMPRESSED_ESCAPE
;;          |                 |
;;          |                 v
;;     CHUNK_REPVAL <-- CHUNK_REPCOUNT
;;
;; ----------------------------------------------------------------------------

;; ============================================================================
;; Macro to set IX (current state). This is done by assigning only IX.low,
;; and can only be used when both states are in the same RAM page (same high
;; address byte). In other cases, simply use LD IX, #TO instead.
;; ============================================================================

    .macro SWITCH_STATE FROM TO

    .dw  LD_IX_LOW
    .db  <(TO)

    ;; This should ideally yield an error for states on different pages,
    ;; but the wonky sdasz80 doesn't seem to catch this. However,
    ;; it does yield an error when these two are in different segments.

    .ds  >(FROM)->(TO)     ;; normally zero, but one of these should be negative
    .ds  >(TO)->(FROM)     ;; if the two routines are on different pages

    ;; So place all state routines in segment _LAST_PART_OF_ROM
    ;; and use the .map file to verify that this segment starts at
    ;; 0x700 or higher. Then this macro _should_ be safe.

    .endm


;; ############################################################################
;; check_limits_and_load_byte
;;
;; Checks whether the current chunk or the loaded TFTP data have been consumed.
;; If they have, returns with Z flag set (and possibly state changed).
;; If they have not, reads another byte into A, and returns with Z cleared.
;; ############################################################################

    .area _CODE

  ;; -------------------------------------------------------------------------
  ;; Helper for end of current chunk: switch state
  ;; -------------------------------------------------------------------------

chunk_done:

  ld   ix, #s_chunk_header
  ret

check_limits_and_load_byte:

  ;; -------------------------------------------------------------------------
  ;; reached end of current chunk?
  ;; -------------------------------------------------------------------------

  ld   a, h
  or   a, l
  jr   z, chunk_done

  ;; -------------------------------------------------------------------------
  ;; reached end of loaded TFTP data?
  ;; -------------------------------------------------------------------------

  ld   a, b
  or   a, c
  ret  z

  ;; FALL THROUGH to load_byte_from_chunk

;; ############################################################################
;; load_byte_from_chunk
;;
;; like load_byte_from_packet, but also decreases HL
;; ############################################################################

load_byte_from_chunk:

    dec   hl

    ;; FALL THROUGH to load_byte_from_packet


;; ############################################################################
;; load_byte_from_packet
;; ############################################################################

load_byte_from_packet:

    ld   a, (iy)
    inc  iy

    dec  bc

    ret

;; ############################################################################
;; State HEADER (initial):
;;
;; Evacuates the header from the TFTP data block and sets up the next state.
;; ############################################################################

    .area _NONRESIDENT

s_header:

    push bc

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until prepare_context is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #stored_snapshot_header
    ld   bc, #Z80_HEADER_RESIDENT_SIZE

    ldir

    ;; ------------------------------------------------------------------------
    ;; check snapshot header
    ;; ------------------------------------------------------------------------

    ld    a, #48                  ;; initial assumption, possibly revised below
    ld    (kilobytes_expected), a

    ;; set DE to .z80 snapshot header size
    ;; (initially the snapshot v1 size, modified later below)

    ld   de, #Z80_HEADER_OFFSET_EXT_LENGTH

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l
    jr   z, s_header_ext_hdr               ;; extended header?

    ;; ------------------------------------------------------------------------
    ;; Assume a single 48k chunk.
    ;; Decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK

    ;; COMPRESSED flag set   =>  A != 0  =>  Z == 0  =>  s_chunk_write_data_compressed
    ;; COMPRESSED flag clear =>  A == 0  =>  Z == 1  =>  s_chunk_write_data_uncompressed

    call set_compression_state
    jr   s_header_set_state

s_header_ext_hdr:

    ;; ------------------------------------------------------------------------
    ;; extended header: adjust expected no. of kilobytes for a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_HW_TYPE)
    cp    a, #SNAPSHOT_128K
    jr    c, s_header_not_128k
    ld    a, #128
    ld    (kilobytes_expected), a

s_header_not_128k:

    ;; ------------------------------------------------------------------------
    ;; adjust header length, keep in DE
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   hl, de
    inc   hl
    inc   hl
    ex    de, hl

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    ;; SWITCH_STATE  s_header  s_chunk_header
    ld   ix, #s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; adjust IY and BC for header size
    ;; ------------------------------------------------------------------------

    pop  hl            ;; was pushed as BC above

    add  iy, de        ;; clears C flag (as IY > DE)
    sbc  hl, de        ;; so no carry here

    ld   b, h
    ld   c, l

    ;; ------------------------------------------------------------------------
    ;; Set up register defaults for a single 48k chunk. For a version 2+
    ;; snapshot these values will be superseded in the chunk header.
    ;; ------------------------------------------------------------------------

    ;; HL needs to be at least 0xC000, to ensure all bytes in the chunk are
    ;; loaded. A larger value is OK, since the context switch will take over
    ;; after 48k have been loaded anyway.

    ld   h, #0xC0      ;; ensure HL >= 0xC000
    ld   de, #0x4000

    ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

    .area _CODE

s_chunk_compressed_escape:

    call  load_byte_from_chunk

    ;; tentative next state

    ;; SWITCH_STATE  s_chunk_compressed_escape  s_chunk_repcount
    ld    ix, #s_chunk_repcount

    cp    a, #Z80_ESCAPE
    ret   z

    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not an escape sequence
    ;;

    ex    af, af'

    ld    a, #Z80_ESCAPE

    call  store_byte_and_update_progress

    ex    af, af'

    SWITCH_STATE  s_chunk_repcount  s_chunk_write_data_compressed
    ;; ld    ix, #s_chunk_write_data_compressed

    ;; FALL THROUGH to store_byte_and_update_progress


;; ############################################################################
;; store_byte_and_update_progress
;;
;; Store byte A in *(DE++), and continue with update_progress
;; ############################################################################

store_byte_and_update_progress:

    ld    (de), a
    inc   de

    ;; FALL THROUGH to update_progress


;; ############################################################################
;; update_progress
;;
;; If the number of bytes loaded reached an even kilobyte,
;; increase kilobyte counter and update status display
;; ############################################################################

update_progress:

    ;; check if DE is an integral number of kilobytes,
    ;; return early otherwise

    ld    a, d
    and   a, #0x03
    or    a, e
    ret   nz

    ;; ------------------------------------------------------------------------
    ;; use alternate BC, DE, HL for scratch here
    ;; ------------------------------------------------------------------------

    exx

    ;; ========================================================================
    ;; update the progress display
    ;; ========================================================================

    ld    hl, #_digits
    ld    a, (hl)
    inc   a
    daa
    push  af             ;; remember flags
    ld    (hl), a
    ld    c, a

    ;; If Z is set, then the number of kilobytes became zero in BCD:
    ;; means it just turned from 99 to 100.
    ;; Print the digit '1' for hundreds.

    ld    l, #10
    rla                        ;; make A := 1 without affecting Z
    call  z, show_attr_digit
    ld    a, c

    pop   de             ;; recall flags, old F is now in E
    bit   #4, e          ;; was H flag set? Then the tens have increased
    jr    z, not_10k

    ;; Print tens (_x_)

    rra                  ;; shift once; routine below shifts three more times
    ld    l, #17
    call  show_attr_digit_already_shifted

not_10k:
    ;; Print single-number digit (__x)

    ld    a, c
    call  show_attr_digit_right

    ;; ************************************************************************
    ;; update progress bar
    ;; ************************************************************************

    ;; kilobytes_loaded is located directly after kilobytes_expected, so
    ;; use a single pointer

    ld    hl, #kilobytes_expected

    ld    a, (hl)                      ;; load kilobytes_expected
    inc   hl                           ;; now points to kilobytes_loaded
    inc   (hl)                         ;; increase kilobytes_loaded
    add   a, a                         ;; sets carry if this is a 128k snapshot
    ld    a, (hl)                      ;; kilobytes_loaded

    ;; ------------------------------------------------------------------------
    ;; Scale loaded number of kilobytes to a value 0..32.
    ;; 48k snapshots:   * 2 / 3
    ;; 128k snapshots:  * 1 / 4 
    ;; ------------------------------------------------------------------------

    ld    b, #4

    jr    c, progress_128

    add   a, a       ;; 48 snapshot: * 2 / 3
    dec   b

progress_128:

    call  a_div_b
    ld    a, c

00002$:
    or    a, a
    jr    z, no_progress_bar

    ld    bc, #PROGRESS_BAR_BASE-1
    add   a, c
    ld    c, a
    ld    a, #(GREEN + (GREEN << 3))
    ld    (bc), a

    ;; ========================================================================
    ;; if all data has been loaded, perform the context switch
    ;; ========================================================================

    ld    a, (hl)
    dec   hl
    cp    a, (hl)
    jp    z, context_switch

no_progress_bar:

    exx

    ;; ========================================================================
    ;; handle evacuation of resident data (0x5800..0x5fff)
    ;; ========================================================================

    ld    a, d

    cp    a, #>RUNTIME_DATA
    jp    z, start_storing_runtime_data

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
    ret   nz

    ;; ------------------------------------------------------------------------
    ;; use register R, bit 7 to indicate whether the evacuation is already
    ;; done (R==0 after reset)
    ;; ------------------------------------------------------------------------

    ld    a, r
    ret   m          ;; return if R bit 7 is 1
    cpl              ;; R bit 7 was 0, is now 1
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; use alternate BC, DE, HL for scratch here
    ;; ------------------------------------------------------------------------

    exx
    SETUP_CONTEXT_SWITCH
    exx

start_storing_runtime_data:
    ld    d, #>EVACUATION_TEMP_BUFFER
    ret


;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive first byte in chunk header: low byte of chunk length
;; ############################################################################

    .area _LAST_PART_OF_ROM

s_chunk_header:

    call load_byte_from_packet
    ld   l, a

    SWITCH_STATE  s_chunk_header  s_chunk_header2
    ;; ld   ix, #s_chunk_header2

    ret


;; ############################################################################
;; state CHUNK_HEADER2:
;;
;; receive second byte in chunk header: high byte of chunk length
;; ############################################################################

    .area _LAST_PART_OF_ROM

s_chunk_header2:

    call load_byte_from_packet
    ld   h, a

    SWITCH_STATE  s_chunk_header2  s_chunk_header3
    ;; ld   ix, #s_chunk_header3

    ret

;; ############################################################################
;; state CHUNK_HEADER3:
;;
;; receive third byte in chunk header:
;; ID of the page the chunk belongs to (range is 3..10)
;;
;; See:
;; https://www.worldofspectrum.org/faq/reference/z80format.htm
;; https://www.worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
;; ############################################################################

    .area _LAST_PART_OF_ROM

s_chunk_header3:

    ;; =======================================================================
    ;; This state selects a value for DE (0x4000, 0x8000, 0xC000).
    ;; For a 128k snapshot, a matching 128k memory configuration is also set.
    ;; =======================================================================

    call load_byte_from_packet

    ;; -----------------------------------------------------------------------
    ;; The loaded byte is an id in range 3..10, as described here:
    ;; https://worldofspectrum.org/faq/reference/z80format.htm
    ;;
    ;; Map this to a 128k page ID in range 0..7.
    ;; -----------------------------------------------------------------------

    sub  a, #3

    ;; -----------------------------------------------------------------------
    ;; Need to handle page 5 separately -- if we do not use the address range
    ;; 0x4000..0x7fff, the evacuation stuff will not work.
    ;; -----------------------------------------------------------------------

    ld   d, #0x40
    cp   a, #5
    jr   z, s_chunk_header3_set_comp_mode

    ;; -----------------------------------------------------------------------
    ;; Remaining handling is done differently for 48k and 128 snapshots.
    ;; -----------------------------------------------------------------------

    ld   d, #0xc0
    ld   e, a              ;; save page ID (0..7)

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    cp   a, #SNAPSHOT_128K ;; is this a 128k snapshot?

    jr   nc, s_chunk_header3_128k_banking

    ;; -----------------------------------------------------------------------
    ;; This is a 48k snapshot:
    ;; 
    ;;   A == 1  means bank 1, to be mapped to 0x8000
    ;;   A == 2  means bank 2, to be mapped to 0xC000
    ;;
    ;; (other pages not expected in 48 snapshots)
    ;; -----------------------------------------------------------------------

    dec  e                                      ;; was this page 1?
    jr   nz, s_chunk_header3_set_comp_mode      ;; then 0xC000 is fine

    ld   d, #0x80

    ;; -----------------------------------------------------------------------
    ;; FALL THROUGH to 128k memory configuration here:
    ;;
    ;; E==0, so page 0 will be paged in at 0xC000 (which is the same page
    ;; selected in init.asm and in context switch). Shouldn't matter much
    ;; anyway, since DE will now be 0x8000, and the 0xC000..0xFFFF addresses
    ;; shouldn't be touched.
    ;;
    ;; The memory configuration will be updated in the context switch anyway.
    ;; -----------------------------------------------------------------------

s_chunk_header3_128k_banking:

    ;; -----------------------------------------------------------------------
    ;; This is one of banks 0..4 or 6..7 in 128k snapshot:
    ;; set memory configuration accordingly
    ;;
    ;; https://worldofspectrum.org/faq/reference/128kreference.htm
    ;; -----------------------------------------------------------------------

    push bc
    ld   bc, #MEMCFG_ADDR
    out  (c), e
    pop  bc

s_chunk_header3_set_comp_mode:

    ld   e, #0

    ;; -----------------------------------------------------------------------
    ;; https://worldofspectrum.org/faq/reference/z80format.htm :
    ;;
    ;; If chunk length=0xffff, data is 16384 bytes long and not compressed
    ;; -----------------------------------------------------------------------

    ld   a, h
    and  a, l
    inc  a

    ;; If chunk length is 0xffff, Z will now be set,
    ;; and state s_chunk_write_data_uncompressed is selected

    ;; Otherwise s_chunk_write_data_compressed is selected

    ;; FALL THROUGH to set_compression_state


;; ############################################################################
;; set_compression_state
;;
;; Sets the next state depending on Z flag. If s_chunk_write_data_uncompressed
;; is selected, HL (bytes left in chunk) is set to 0x4000.
;;
;; Z == 0: s_chunk_write_data_compressed
;; Z == 1: s_chunk_write_data_uncompressed
;; ############################################################################

set_compression_state:
    ld    ix, #s_chunk_write_data_compressed
    ret   nz

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_write_data_uncompressed
    ;; ld    ix, #s_chunk_write_data_uncompressed

    ld    hl, #0x4000
    ret   z


;; ############################################################################
;; state CHUNK_WRITE_DATA_UNCOMPRESSED
;; ############################################################################

s_chunk_write_data_uncompressed:

  call check_limits_and_load_byte
  jr   nz, store_byte

  ret


;; ############################################################################
;; state CHUNK_REPCOUNT
;; ############################################################################

    .area _LAST_PART_OF_ROM

s_chunk_repcount:

    call load_byte_from_chunk

    ld   (_repcount), a

    SWITCH_STATE  s_chunk_repcount  s_chunk_repvalue
    ;; ld   ix, #s_chunk_repvalue

    ret


;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _LAST_PART_OF_ROM

s_chunk_repvalue:

    call load_byte_from_chunk
    ld   i, a

    SWITCH_STATE  s_chunk_repvalue  s_chunk_write_data_compressed
    ;; ld   ix, #s_chunk_write_data_compressed

    ;; FALL THROUGH to s_chunk_write_data_compressed


;; ############################################################################
;; state CHUNK_WRITE_DATA_COMPRESSED
;; ############################################################################

s_chunk_write_data_compressed:

  ;; -------------------------------------------------------------------------
  ;; Check the repetition count. This is zero when no repetition is active.
  ;; -------------------------------------------------------------------------

  ld   a, (_repcount)
  or   a, a
  jr   nz, do_repetition

  call check_limits_and_load_byte
  ret  z

  ;; -------------------------------------------------------------------------
  ;; check for the escape byte of a repetition sequence
  ;; -------------------------------------------------------------------------

  cp   a, #Z80_ESCAPE
  jr   z, chunk_escape

store_byte:

  call store_byte_and_update_progress

jp_ix_instr:

  jp   (ix)   ;; one of s_chunk_write_data_compressed  or  ..._uncompressed

  ;; -------------------------------------------------------------------------
  ;; a non-zero number of repetitions remain:
  ;; decrease repetition count and write the repetition value to memory
  ;; -------------------------------------------------------------------------

do_repetition:

  dec  a
  ld   (_repcount), a

  ld   a, i

  jr   store_byte

  ;; -------------------------------------------------------------------------
  ;; Escape byte found: switch state
  ;; -------------------------------------------------------------------------

chunk_escape:

  ;; SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_compressed_escape
  ld   ix, #s_chunk_compressed_escape

  ret
