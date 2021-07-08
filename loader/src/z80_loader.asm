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
;; Three bytes located together, so two of them can be accessed in a single
;; 16-bit word (kilobytes_loaded+expected, or kilobytes_expected+ram_config)
;; ----------------------------------------------------------------------------

kilobytes_loaded:
    .ds   1          ;; currently loaded no. of kilobytes, for progress display
kilobytes_expected_and_memory_config:
kilobytes_expected:
    .ds   1          ;; expected no. of kilobytes (48 or 128)
ram_config:
    .ds   1          ;; 128k RAM configuration (to be written to I/O 0x7ffd)

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

    .globl FROM       ;; ensure these can be checked in the linker's map output
    .globl TO

    .dw  LD_IX_LOW
    .db  <(TO)

    ;; This should ideally yield an error for states on different pages,
    ;; but the wonky sdasz80 doesn't seem to catch this. However,
    ;; it does yield an error when these two are in different segments.

    .ds  >(FROM)->(TO)     ;; normally zero, but one of these should be negative
    .ds  >(TO)->(FROM)     ;; if the two routines are on different pages

    ;; So place all state routines in segment _Z80_LOADER_STATES
    ;; and use the .map file to verify that this segment starts at
    ;; 0x700 or higher. Then this macro _should_ be safe.

    .endm

    .macro SWITCH_STATE_SAFE FROM TO
    ld   ix, #TO
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

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_header
    ;; ld   ix, #s_chunk_header
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
;; The actual entrypoint follows below, and then JRs backwards in memory.
;; This could potentially allow the s_header address to be kept on the same
;; page as the other .z80 loader states. (Fragile.)
;; ############################################################################

    .area _Z80_LOADER_STATES

    ;; ========================================================================
    ;; NOTE: actual entrypoint s_header further below
    ;; ========================================================================

s_header_ext_hdr:

    ;; ========================================================================
    ;; snapshot version 2+: is it for a 128k machine?
    ;; ========================================================================

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)

    ld   a, l
    cp   a, #SNAPSHOT_128K
    jr   c, s_header_not_128k

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot detected: load memory config into D, and set E to 128
    ;; ------------------------------------------------------------------------

    ld   d, h
    ld   e, #128

s_header_not_128k:

    ;; ------------------------------------------------------------------------
    ;; adjust header length, keep in BC
    ;;
    ;; A byte addition is sufficient, as the sum will always be <= 0xF2
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   a, c
    inc   a
    inc   a
    ld    c, a

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    SWITCH_STATE  s_header  s_chunk_header
    ;; ld   ix, #s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; store memory configuration and number of kilobytes expected
    ;; ------------------------------------------------------------------------

    ld   (kilobytes_expected_and_memory_config), de

    ;; ------------------------------------------------------------------------
    ;; adjust IY and BC for header size
    ;; ------------------------------------------------------------------------

    add  iy, bc

    ;; ------------------------------------------------------------------------
    ;; Safely assume that the TFTP packet is 0x200 bytes in length, as the RFC
    ;; guarantees this for all packets but the last one.
    ;;
    ;; Set up BC as (0x0200 - C), that is,
    ;;
    ;;   C := 0 - C   and
    ;;   B := 1.
    ;;
    ;; B is currently 0 (after initial LDIR above).
    ;; ------------------------------------------------------------------------

    xor  a, a
    sub  a, c       ;; no carry expected, as C is at most 54 (0x36)
    ld   c, a
    inc  b          ;; B is now 1

    ;; ------------------------------------------------------------------------
    ;; Set up DE for a single 48k chunk, to be loaded at 0x4000. For a version
    ;; 2+ snapshot this address will be superseded in s_chunk_header3.
    ;; ------------------------------------------------------------------------

    ld   de, #0x4000

    ret

;; ############################################################################
;; actual entry point
;; ############################################################################

s_header:

    ;; ------------------------------------------------------------------------
    ;; clear out attribute line 23 for progress bar
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5ae0        ;; attribute line 23
    ld   de, #0x5ae1
    ld   (hl), #WHITE + (WHITE << 3) + BRIGHT
    ld   bc, #0x1f

    ldir

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until prepare_context is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   e, #<stored_snapshot_header               ;; E == 0x5B from LDIR above
    ld   c, #Z80_HEADER_RESIDENT_SIZE                 ;; B == 0 from LDIR above

    ldir

    ;; ------------------------------------------------------------------------
    ;; BC: .z80 header size
    ;; D: memory configuration value for 0x7ffd
    ;; E: number of kilobytes expected (48 or 128)
    ;; ------------------------------------------------------------------------

    ld   c, #Z80_HEADER_OFFSET_EXT_LENGTH      ;; B==0 here

    ;; -----------------------------------------------------------------------
    ;; Memory configuration for a 48k snapshot on a 128k machine. Bits are
    ;; set as follows:
    ;;
    ;; Bits 0..2 := 0:  page 0 at 0xC000
    ;; Bit 3     := 0:  normal screen (page 5)
    ;; Bit 4     := 1:  48k BASIC ROM
    ;; Bit 5     := 1:  lock memory paging
    ;;
    ;; https://worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
    ;; -----------------------------------------------------------------------

    ld   de, #((MEMCFG_ROM_48K + MEMCFG_LOCK) << 8)  + 48

    ;; ------------------------------------------------------------------------
    ;; check snapshot header version
    ;; ------------------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l
    jr   z, s_header_ext_hdr               ;; extended header?

    ;; ------------------------------------------------------------------------
    ;; This is a v1 snapshot. Store PC value in Z80_HEADER_OFFSET_EXT_PC,
    ;; to read it unambiguously in context_switch.
    ;; ------------------------------------------------------------------------

    ld   (stored_snapshot_header + Z80_HEADER_OFFSET_EXT_PC), hl

    ;; ------------------------------------------------------------------------
    ;; Assume a single 48k chunk, without header.
    ;; Decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS
    bit  5, (hl)    ;; COMPRESSED flag set?

    ;; COMPRESSED flag set   =>  Z == 0  =>  s_chunk_write_data_compressed
    ;; COMPRESSED flag clear =>  Z == 1  =>  s_chunk_write_data_uncompressed

    call set_compression_state

    ;; Ensure HL is at least 0xC000, so all bytes in the chunk are loaded.
    ;; A larger value is OK, since the context switch will take over after 48k
    ;; have been loaded anyway.

    ld   h, #0xC0

    jr   s_header_set_state


;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive first byte in chunk header: low byte of chunk length
;; ############################################################################

    .area _Z80_LOADER_STATES

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

    .area _Z80_LOADER_STATES

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

    .area _Z80_LOADER_STATES

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
    ;; If chunk length=0xffff, data is 16384 bytes long and not compressed.
    ;; There is no other way that the high byte could be 0xFF here, so only
    ;; H needs to be checked.
    ;; -----------------------------------------------------------------------

    ld   a, h
    inc  a

    ;; If chunk length is 0xffxx, Z will now be set,
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
    SWITCH_STATE  s_header  s_chunk_write_data_compressed
    ;; ld    ix, #s_chunk_write_data_compressed
    ret   nz

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_write_data_uncompressed
    ;; ld    ix, #s_chunk_write_data_uncompressed

    ld    hl, #0x4000
    ret


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

    .area _Z80_LOADER_STATES

s_chunk_repcount:

    call load_byte_from_chunk

    ld   (_repcount), a

    SWITCH_STATE  s_chunk_repcount  s_chunk_repvalue
    ;; ld   ix, #s_chunk_repvalue

    ret


;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _Z80_LOADER_STATES

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

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_compressed_escape
    ;; ld   ix, #s_chunk_compressed_escape

    ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

    .area _Z80_LOADER_STATES

s_chunk_compressed_escape:

    call  load_byte_from_chunk

    ;; tentative next state

    SWITCH_STATE  s_chunk_compressed_escape  s_chunk_repcount
    ;; ld    ix, #s_chunk_repcount

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

    ;; Print tens (_x_)

    rra
    ld    l, #17
    call  nz, show_attr_digit_already_shifted

    ;; Print single-number digit (__x)

    ld    a, c
    call  show_attr_digit_right

    ;; ========================================================================
    ;; update progress bar
    ;; ========================================================================

    ;; kilobytes_loaded is located directly before kilobytes_expected, so
    ;; use a single pointer

    ld    hl, #kilobytes_expected

    ld    a, (hl)                      ;; load kilobytes_expected
    dec   hl                           ;; now points to kilobytes_loaded
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

    ld    a, (hl)                      ;; kilobytes_loaded
    inc   hl
    cp    a, (hl)                      ;; kilobytes_expected
    jr    z, perform_context_switch

no_progress_bar:

    exx

    ;; ========================================================================
    ;; handle evacuation of resident data (0x5800..0x5fff)
    ;; ========================================================================

    ld    a, d

    cp    a, #>RUNTIME_DATA
    jr    z, start_storing_runtime_data

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

perform_context_switch:
    PERFORM_CONTEXT_SWITCH