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

;; ----------------------------------------------------------------------------
;; digits (BCD) for progress display while loading a snapshot
;; ----------------------------------------------------------------------------

_digits:
    .ds   1

;; ----------------------------------------------------------------------------
;; flag indicating whether SETUP_CONTEXT_SWITCH has been executed
;; ----------------------------------------------------------------------------

is_context_switch_set_up:
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
;;                     |      CHUNK_HEADER <----------------------------\
;;                     |           |                                    |
;;                     v           v                                    |
;;                     |      CHUNK_HEADER2                             |
;;                     |           |                                    |
;;                     |           v                                    ^
;;                     |      CHUNK_HEADER3                             |
;;                     |           |                                    |
;;                     \--v--------/                                    |
;;                        |                                             |
;;                        |                                             |
;;                        +--------> CHUNK_WRITE_DATA_UNCOMPRESSED -->--+
;;                        |                                             |
;;                        v                                             |
;;    REPETITION ------> CHUNK_WRITE_DATA_COMPRESSED ----------->-------/ 
;;        |                 |        ^
;;        |                 v        |
;;        ^               CHUNK_COMPRESSED_ESCAPE -----<-----\
;;        |                   |             |                |
;;        |                   v             v                |
;;   CHUNK_REPVAL <-- CHUNK_REPCOUNT      CHUNK_COMPRESSED_ESCAPE_FALSE
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
;; Checks whether the current chunk or the loaded TFTP packet has been
;; fully consumed.
;; If it has, return to the caller's caller (that is, the state loop).
;; Otherwise, read another byte into A, and set Z if A == Z80_ESCAPE (0xED).
;; ############################################################################

    .area _CODE

  ;; -------------------------------------------------------------------------
  ;; Helper for end of current chunk: switch state
  ;; -------------------------------------------------------------------------

chunk_done:

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_header
    ;; ld   ix, #s_chunk_header

  ;; -------------------------------------------------------------------------
  ;; Helper: leave current state and return to state loop
  ;; -------------------------------------------------------------------------

chunk_return_from_state:
    pop  af                  ;; pop return address (s_xxx)
    ret                      ;; return to state loop

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
  jr   z, chunk_return_from_state

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

    cp   a, #Z80_ESCAPE

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
    ;; A byte addition is sufficient, as the sum will always be
    ;; <= (Z80_HEADER_OFFSET_EXT_LENGTH + 2 + 55)
    ;; <= 0x57
    ;;
    ;; see  https://worldofspectrum.org/faq/reference/z80format.htm  about
    ;; possible values for the header length here
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   a, #Z80_HEADER_OFFSET_EXT_LENGTH + 2
    ld    c, a

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    SWITCH_STATE  s_header  s_chunk_header
    ;; ld   ix, #s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; store memory configuration and number of kilobytes expected
    ;; assumes kilobytes_expected and ram_config to be stored consecutively
    ;; ------------------------------------------------------------------------

    ld   (kilobytes_expected), de                     ;; also writes ram_config

    ;; ------------------------------------------------------------------------
    ;; adjust IY and BC for header size
    ;; ------------------------------------------------------------------------

    add  iy, bc

    ;; ------------------------------------------------------------------------
    ;; Safely assume that the TFTP packet is 0x200 bytes in length, as the RFC
    ;; guarantees this for all packets but the last one. And the first packet
    ;; cannot be the last, as 48k in maximal compression would require
    ;; 5 * (49152 / 255) == 964 bytes.
    ;;
    ;; Set up BC as (0x0200 - C), that is,
    ;;
    ;;   C := 0 - C   and
    ;;   B := 1.
    ;;
    ;; B is currently 0 (after initial LDIRs), and 0 < C <= 0x57.
    ;; ------------------------------------------------------------------------

    xor  a, a
    sub  a, c       ;; no carry expected, as C is at most 87 (0x57)
    ld   c, a
    inc  b          ;; B is now 1

    ;; ------------------------------------------------------------------------
    ;; Set up DE and HL for a single 48k chunk, to be loaded at 0x4000. For a
    ;; version 2+ snapshot these values will be superseded in s_chunk_header
    ;; and s_chunk_header3, so a version 1 snapshot is assumed here.
    ;; ------------------------------------------------------------------------

    ld   de, #0x4000

    ;; ------------------------------------------------------------------------
    ;; Ensure HL is at least 0xC000, so all bytes in the chunk are loaded.
    ;; A larger value is OK, since the context switch will take over after 48k
    ;; have been loaded anyway.
    ;;
    ;; This only matters for a version 1 snapshot, and C is then
    ;; (0x0100 - Z80_HEADER_OFFSET_EXT_LENGTH) = 0xe2.
    ;; ------------------------------------------------------------------------

    ld   h, c       ;; HL := 0xE2xx

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
    ld   e, #<stored_snapshot_header               ;; D == 0x5B from LDIR above
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

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #0x20       ;; COMPRESSED flag set?

    ;; COMPRESSED flag set   =>  Z == 0  =>  s_chunk_write_data_compressed
    ;; COMPRESSED flag clear =>  Z == 1  =>  s_chunk_write_data_uncompressed

    call set_compression_state

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

    ld   d, a
    ld   a, (kilobytes_expected)
    add  a, a                            ;; set carry flag for a 128k snapshot
    ld   a, d

    ld   d, #0xc0

    jr   c, s_chunk_header3_128k_banking

    ;; -----------------------------------------------------------------------
    ;; This is a 48k snapshot:
    ;; 
    ;;   A == 1  means bank 1, to be mapped to 0x8000
    ;;   A == 2  means bank 2, to be mapped to 0xC000
    ;;
    ;; (other pages not expected in 48 snapshots; page 5 handled above)
    ;; -----------------------------------------------------------------------

                    ;; bank 1    bank 2
    inc  a          ;; 0x02      0x03
    rrca            ;; 0x01      0x81
    rrca            ;; 0x80      0xc0
    ld   d, a

    ;; -----------------------------------------------------------------------
    ;; FALL THROUGH to 128k memory configuration here:
    ;;
    ;; At this point, A==0x80 or 0xc0, but bits 6..7 are not used in the
    ;; 128k paging register. Lower bits are zero, so page 0 will be paged in
    ;; at 0xC000 (which is the same page selected in init.asm and in 48k
    ;; context switch).
    ;; -----------------------------------------------------------------------

s_chunk_header3_128k_banking:

    ;; -----------------------------------------------------------------------
    ;; This is one of banks 0..4 or 6..7 in 128k snapshot:
    ;; set memory configuration accordingly
    ;;
    ;; https://worldofspectrum.org/faq/reference/128kreference.htm
    ;; -----------------------------------------------------------------------

    exx
    ld   bc, #MEMCFG_ADDR
    out  (c), a
    exx

s_chunk_header3_set_comp_mode:

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
;; Z == 1: s_chunk_write_data_uncompressed, HL := 0x4000
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

    jr   store_byte


;; ############################################################################
;; state CHUNK_REPCOUNT
;; ############################################################################

    .area _Z80_LOADER_STATES

s_chunk_repcount:

    call check_limits_and_load_byte

    ;; -------------------------------------------------------------------------
    ;; store the repetition count in (DE), and read it back to A' in next state
    ;; -------------------------------------------------------------------------

    ld   (de), a

    SWITCH_STATE  s_chunk_repcount  s_chunk_repvalue

    ;; FALL THROUGH to s_chunk_repvalue


;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _Z80_LOADER_STATES

s_chunk_repvalue:

    call check_limits_and_load_byte

    ;; -------------------------------------------------------------------------
    ;; the loaded byte does not need to be stored here:
    ;; it is available as -1(iy) when needed
    ;; -------------------------------------------------------------------------

    SWITCH_STATE  s_chunk_repvalue  s_repetition

    ;; -------------------------------------------------------------------------
    ;; recall the repetition count, store in A'
    ;; -------------------------------------------------------------------------

    ld   a, (de)
    ex   af, af'

    ;; FALL THROUGH to s_repetition


;; ############################################################################
;; state REPETITION
;; ############################################################################

s_repetition:

    ;; -------------------------------------------------------------------------
    ;; the byte to repeat is always the most recently loaded one, as this state
    ;; (s_repetition) does not involve any loading of data (only writing)
    ;; -------------------------------------------------------------------------

    ld   a, -1(iy)

    ;; -------------------------------------------------------------------------
    ;; check the repetition count
    ;; -------------------------------------------------------------------------

    ex   af, af'
    dec  a
    jr   nz, switch_af_and_store_byte

    ;; FALL THROUGH to s_chunk_compressed_escape_false


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE_FALSE
;;
;; invoked when an ED byte was followed by something else than ED
;; (in other words, a false escape sequence)
;;
;; (The same code sequence also happens to work for the state transition in
;; s_repetition above.)
;; ############################################################################

s_chunk_compressed_escape_false:

    ;; -------------------------------------------------------------------------
    ;; return to s_chunk_write_data_compressed when this byte has been written
    ;; -------------------------------------------------------------------------

    SWITCH_STATE  s_repetition  s_chunk_write_data_compressed

switch_af_and_store_byte:

    ;; -------------------------------------------------------------------------
    ;; in s_chunk_compressed_escape_false: recall the non-escape, non-ED byte
    ;; in s_repetition: recall the repetition value
    ;; -------------------------------------------------------------------------

    ex    af, af'

    jr   store_byte


;; ############################################################################
;; state CHUNK_WRITE_DATA_COMPRESSED         (presumably the most common state)
;; ############################################################################

s_chunk_write_data_compressed:

    call check_limits_and_load_byte

    ;; -------------------------------------------------------------------------
    ;; check for the escape byte of a repetition sequence
    ;; -------------------------------------------------------------------------

    jr   z, chunk_escape            ;; Z set if A == Z80_ESCAPE

store_byte:

    ld    (de), a
    inc   de

    ld    a, d
    and   a, #0x03
    or    a, e
    jr    z, update_progress

jp_ix_instr:

    jp   (ix)   ;; one of
                ;;   s_chunk_write_data_compressed,
                ;;   s_chunk_write_data_uncompressed,
                ;;   s_repetition

    ;; -------------------------------------------------------------------------
    ;; Escape byte found: switch state
    ;; -------------------------------------------------------------------------

chunk_escape:

    SWITCH_STATE  s_chunk_write_data_compressed  s_chunk_compressed_escape
    ;; ld   ix, #s_chunk_compressed_escape

    ;; FALL THROUGH to s_chunk_compressed_escape


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

s_chunk_compressed_escape:

    ;; -----------------------------------------------------------------------
    ;; One escape byte has been loaded; check the next one.
    ;; If that byte is also an escape byte, handle the ED ED compression
    ;; sequence. If not, write the initial (misleading) ED byte to RAM
    ;; and continue in state s_chunk_compressed_escape_false.
    ;; -----------------------------------------------------------------------

    call  check_limits_and_load_byte

    ;; -----------------------------------------------------------------------
    ;; Z flag now indicates whether the next char is also Z80_ESCAPE (0xED)
    ;; -----------------------------------------------------------------------

    SWITCH_STATE  s_chunk_compressed_escape  s_chunk_repcount

    ret   z                                       ;; return if A == Z80_ESCAPE

    ;; -----------------------------------------------------------------------
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not an escape sequence
    ;; -----------------------------------------------------------------------

    ex    af, af'                         ;; store the non-escape, non-ED byte

    ;; -----------------------------------------------------------------------
    ;; store the (non-escape) ED byte, switch state
    ;; -----------------------------------------------------------------------

    ld    a, #Z80_ESCAPE

    SWITCH_STATE  s_chunk_repcount  s_chunk_compressed_escape_false

    jr    store_byte


;; ############################################################################
;; update_progress
;;
;; increase kilobyte counter and update status display
;; ############################################################################

update_progress:

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

    ;; ========================================================================
    ;; if all data has been loaded, perform the context switch
    ;; ========================================================================

    cp    a, (hl)
    jp    z, perform_context_switch    ;; FIXME: could this be a JR instead?

    ;; ------------------------------------------------------------------------
    ;; Scale loaded number of kilobytes to a value 0..32.
    ;; 48k snapshots:   (k + 1) * 2 / 3
    ;; 128k snapshots:  (k + 1) * 1 / 4
    ;;
    ;; the 'k + 1' addition rounds the progress value up a bit, so as to
    ;; ensure the progress bar reaches its maximum before the context switch
    ;; ------------------------------------------------------------------------

    add   a, a                         ;; sets carry if this is a 128k snapshot

    ld    a, (hl)                      ;; kilobytes_loaded
    inc   a                            ;; 'k + 1', see above

    ld    b, #4

    jr    c, progress_128

    add   a, a       ;; 48 snapshot: * 2 / 3
    dec   b

progress_128:

    call  a_div_b
    ld    a, c
    or    a, a
    jr    z, no_progress_bar

    ld    h, #>(PROGRESS_BAR_BASE-1)
    add   a, #<(PROGRESS_BAR_BASE-1)
    ld    l, a
    ld    (hl), #(GREEN + (GREEN << 3))

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
    ;; use flag 'is_context_switch_set_up' to only set up context switch once,
    ;; first time this address is reached; return silently the second time
    ;; ------------------------------------------------------------------------

    ld    a, (is_context_switch_set_up)
    xor   a, d             ;; becomes non-zero first time, and zero second time
    ret   z
    ld    (is_context_switch_set_up), a

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