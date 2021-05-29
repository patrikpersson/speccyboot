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
    .optsdcc -mz80

    .include "include/z80_loader.inc"

    .include "include/context_switch.inc"
    .include "include/enc28j60.inc"
    .include "include/eth.inc"
    .include "include/globals.inc"
    .include "include/spi.inc"
    .include "include/tftp.inc"
    .include "include/udp_ip.inc"
    .include "include/ui.inc"
    .include "include/util.inc"

;; ============================================================================

Z80_ESCAPE         = 0xED     ;; escape byte in compressed chunks
ATTR_DIGIT_ROW     = 0x5A00   ;; attribute VRAM address for kilobyte counter

PROGRESS_BAR_BASE  = ATTRS_BASE + 0x2E0

;; ----------------------------------------------------------------------------
;; opcodes for patching code at runtime
;; ----------------------------------------------------------------------------

JR_UNCONDITIONAL   = 0x18      ;; JR offset
JR_NZ              = 0x20      ;; JR NZ, offset
JR_Z               = 0x28      ;; JR Z, offset
JP_C               = 0xda      ;; JP C, target
LD_A_N             = 0x3e      ;; LD A, #n
LD_B_N             = 0x06      ;; LD B, #n
LD_IX_NN           = 0x21DD    ;; LD IX, #nn
LD_INDIRECT_HL_N   = 0x36      ;; LD (HL), #n


;; ============================================================================

    .area _DATA

_received_data_length:
    .ds   2       ;; number of valid bytes remaining in received_data

_chunk_bytes_remaining:
    .ds   2       ;; bytes remaining to unpack in current chunk

;; ----------------------------------------------------------------------------
;; expected and currently loaded no. of kilobytes, for progress display
;; ----------------------------------------------------------------------------

_kilobytes_loaded:
    .ds    1

;; ============================================================================

;; ----------------------------------------------------------------------------
;; The Z80 snapshot state machine is implemented by one routine for each
;; state. The function returns whenever one of the following happens:
;;
;; - all currently available data has been consumed (received_data_length == 0)
;; - a state transition is required
;; - the write pointer has reached an integral number of kilobytes
;;   (the outer loop then manages evacuation)
;; ----------------------------------------------------------------------------

    .area _STAGE2

;; ############################################################################
;; show_attr_digit
;;
;; subroutine: show huge digit in attributes, on row ATTR_DIGIT_ROW and down
;; L: column (0..31)
;; A: digit (0..9), bits 4-7 are ignored
;;
;; Destroys AF, B, DE, HL. Returns with L increased by 7.
;; ############################################################################

ROM_DIGITS = 0x3D00 + 16 * 8 + 1 ;; address of '0' bits, first actual scan line

show_attr_digit:

    add   a, a
    add   a, a
    add   a, a

show_attr_digit_already_shifted:  ;; special target for below

    and   a, #0x78           ;; binary 01111000
    add   a, #<ROM_DIGITS    ;; all digits in a single 256b page
    ld    d, #>ROM_DIGITS
    ld    e, a

    ld    h, #>ATTR_DIGIT_ROW

    di    ;; SpeccyBoot is about to be paged out

    ld    a, #SPI_IDLE+SPI_CS+PAGE_OUT
    out   (SPI_OUT), a

show_attr_char_address_known:
00001$:

    ld    a, (de)
    inc   de
    add   a, a
    ld    b, #6
00002$:
    add   a, a
    jr    c, 00004$
    ld    (hl), #WHITE + (WHITE << 3)
    .db   JP_C        ;; C always clear here => ignore the following two bytes
00004$:
    ld    (hl), #BLACK + (BLACK << 3)
00003$:
    inc   hl
    djnz  00002$

    ld    a, #(ROW_LENGTH-6)
    add   a, l
    ld    l, a

    ld    a, #ROW_LENGTH * 6
    cp    a, l
    jr    nc, 00001$

    ld    a, #SPI_IDLE+SPI_CS       ;; page SpeccyBoot back in
    out   (SPI_OUT), a
    ei

    ret


;; ############################################################################
;; _get_next_byte
;;
;; Returns *received_data++ in A
;; also decreases received_data_length
;;
;; (reads byte from (IY), increases received_data, returns byte in A)
;; Modifies HL (but not F)
;; ############################################################################

    .area _STAGE2

_get_next_byte:

    ld   a, (iy)
    inc  iy

    ld   hl, (_received_data_length)
    dec  hl
    ld   (_received_data_length), hl

    ret

;; ############################################################################
;; _dec_chunk_bytes
;;
;; Decreases chunk_bytes_remaining (byte counter in compressed chunk)
;; ############################################################################

    .area _STAGE2

_dec_chunk_bytes:

    ld   hl, (_chunk_bytes_remaining)
    dec  hl
    ld   (_chunk_bytes_remaining), hl
    ret

;; ############################################################################
;; State HEADER (initial):
;;
;; Evacuates the header from the TFTP data block. Returns the length of the
;; header (i.e., the offset of snapshot data within the TFTP data block)
;;
;; This function does some header parsing; it initializes compression_method
;; and verifies compatibility.
;; ############################################################################

    .area _NONRESIDENT

s_header:

    ;; ------------------------------------------------------------------------
    ;; set bank 0, ROM 1 (48K ROM) for 128k memory config while loading
    ;; ------------------------------------------------------------------------

    ld   a, #MEMCFG_ROM_48K
    ld   bc, #MEMCFG_ADDR
    out  (c), a

    ;; ------------------------------------------------------------------------
    ;; check snapshot header
    ;; ------------------------------------------------------------------------

    ;; set DE to .z80 snapshot header size
    ;; (initially the snapshot v1 size, modified later below)

    ld   de, #Z80_HEADER_OFFSET_EXT_LENGTH

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l
    jr   z, s_header_ext_hdr               ;; extended header?

    ;; ------------------------------------------------------------------------
    ;; not an extended header: expect a single 48k chunk
    ;; ------------------------------------------------------------------------

    ld   a, #>0xc000
    ld   (_chunk_bytes_remaining + 1), a       ;; low byte of is already zero

    ;; ------------------------------------------------------------------------
    ;; decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK
    jr   z, s_header_uncompressed_data

    call set_state_compressed
    jr   s_header_set_state

s_header_uncompressed_data:
    call set_state_uncompressed
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
    xor   a, a
    ld    (progress_add_instr), a
    ld    a, #4
    ld    (progress_ratio), a

s_header_not_128k:

    ;; ------------------------------------------------------------------------
    ;; adjust header length
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   hl, de
    inc   hl
    inc   hl
    ex    de, hl

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    ld   ix, #_s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; adjust _received_data and _received_data_length for header size
    ;; ------------------------------------------------------------------------

    add  iy, de

    ld   hl, (_received_data_length)
    sbc  hl, de                      ;; C flag should be clear from ADD above
    ld   (_received_data_length), hl

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until prepare_context is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #_snapshot_header
    ld   bc, #Z80_HEADER_RESIDENT_SIZE
    ldir

    ld   de, #0x4000

    ret


;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive low byte of chunk length
;; ############################################################################

    .area _STAGE2

_s_chunk_header:

    call _get_next_byte
    ld   (_chunk_bytes_remaining), a

    ld    ix, #_s_chunk_header2

    ret


;; ############################################################################
;; state CHUNK_HEADER2:
;;
;; receive high byte of chunk length
;; ############################################################################

    .area _STAGE2

_s_chunk_header2:

    call _get_next_byte
    ld   (_chunk_bytes_remaining + 1), a

    ld    ix, #_s_chunk_header3

    ret

;; ############################################################################
;; state CHUNK_HEADER3:
;;
;; receive ID of the page the chunk belongs to, range is 3..10
;;
;; See:
;; https://www.worldofspectrum.org/faq/reference/z80format.htm
;; https://www.worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
;; ############################################################################

    .area _STAGE2

_s_chunk_header3:

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    ld   c, a

    call _get_next_byte

    cp   a, #3
    jr   c, s_chunk_header3_incompatible
    cp   a, #11
    jr   c, s_chunk_header3_compatible
s_chunk_header3_incompatible:
    ld   a, #FATAL_INCOMPATIBLE
    jp   fail
s_chunk_header3_compatible:

    ;; Decide on a good value for tftp_write_pos; store in DE.

    ld   b, a    ;; useful extra copy of A

    ;;
    ;; Need to handle page 5 separately -- if we do not use the address range
    ;; 0x4000..0x7fff, the evacuation stuff will not work.
    ;;

    ld   d, #0x40
    cp   a, #8                       ;; means page 5 (0x4000..0x7fff)
    jr   z, s_chunk_header3_set_page

    ;;
    ;; Page 1 in a 48k snapshot points to 0x8000, but 128k snapshots are
    ;; different.
    ;;

    ld   d, #0x80
    cp   a, #4                       ;; means page 1 (0x8000..0xbfff)
    jr   nz, s_chunk_header3_default_page

    ld   a, c
    cp   a, #SNAPSHOT_128K    ;; 128k snapshot?
    jr   c, s_chunk_header3_set_page

s_chunk_header3_default_page:

    ld   d, #0xc0
    ld   a, c
    cp   a, #SNAPSHOT_128K
    jr   c, s_chunk_header3_set_page

    ;; If this is a 128k snapshot, switch memory bank

    ld   a, b
    sub  a, #3
    or   a, #MEMCFG_ROM_48K    ;; needed for digits while loading
    ld   bc, #MEMCFG_ADDR
    out  (c), a

s_chunk_header3_set_page:
    ld   e, #0

    ;; If chunk_bytes_remaining is 0xffff, length is 0x4000

    ld   hl, (_chunk_bytes_remaining)
    inc  h
    jr   nz, set_state_compressed
    inc  l
    jr   nz, set_state_compressed

    ld   h, #0x40    ;; HL is now 0x4000
    ld   (_chunk_bytes_remaining), hl

    jr    set_state_uncompressed        ;; FIXME reorder?


;; ############################################################################
;; set_state_compressed
;; ############################################################################

set_state_compressed:

    ;; rewrite JR Z branch for compressed chunks (to react to Z80_ESCAPE bytes)

    ld    a, #chunk_escape - escape_check_branch - 2
    jr    set_state_common


;; ############################################################################
;; set_state_uncompressed
;; ############################################################################

set_state_uncompressed:

    ;; rewrite JR Z branch for uncompressed chunks
    ;; (to not handle Z80_ESCAPE bytes any different from others)

    xor   a, a

set_state_common:

    ld    (escape_check_branch + 1), a
    ld    ix, #s_chunk_write_data

    ret

;; ############################################################################
;; state CHUNK_WRITE_DATA
;; ############################################################################

    .area _STAGE2

s_chunk_write_data::
  xor  a, a        ;; FIXME just to set Z flag

  ;;
  ;; the branch below is patched to JR NZ to
  ;; activate a repetition sequence
  ;;

repetition_branch:
  jr   no_repetition

  .db  LD_A_N             ;; LD A, #n
_repcount:
  .db  0

  or   a, a
  jr   nz, do_repetition

  ;; disable repetition

  ld   a, #JR_UNCONDITIONAL
  ld   (repetition_branch), a

  ret

do_repetition:

  dec  a
  ld   (_repcount), a

  .db  LD_A_N             ;; LD A, #n
_rep_value:
  .db  0

  jr   store_byte

no_repetition:

  ;; end of chunk?

  ld   bc, (_chunk_bytes_remaining)
  ld   a, b
  or   a, c
  jr   z, chunk_done

  ;; return if received_data_length is zero

  ld   hl, (_received_data_length)
  ld   a, h
  or   a, l
  ret  z

  ld   a, (iy)
  inc  iy

  dec  bc
  dec  hl

  ld   (_chunk_bytes_remaining), bc
  ld   (_received_data_length), hl

  cp   a, #Z80_ESCAPE

  ;; -------------------------------------------------------------------------
  ;; this branch is patched to a JR Z, 0 for an uncompressed chunk
  ;; -------------------------------------------------------------------------

escape_check_branch:
  jr   z, chunk_escape

store_byte:

  ld   (de), a
  inc  de

  ;; update progress if DE is on a kilobyte boundary

  ld   a, d
  and  a, #0x03
  or   a, e
  jr   nz, s_chunk_write_data

  jr   update_progress

chunk_done:

  ld   ix, #_s_chunk_header
  ret

chunk_escape:
  ld   ix, #_s_chunk_compressed_escape
  ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

    .area _STAGE2

_s_chunk_compressed_escape:

    call  _get_next_byte
    call  _dec_chunk_bytes

    ld    ix, #_s_chunk_repcount        ;; tentative next state

    cp    a, #Z80_ESCAPE
    ret   z

    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not an escape sequence
    ;;

    ex    af, af'            ;; '

    ld    a, #Z80_ESCAPE
    ld    (de), a
    inc   de

    call  update_progress

    ex    af, af'            ;; '

    ld    (de), a
    inc   de

    ld    ix, #s_chunk_write_data

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

    push  de

    ;; ========================================================================
    ;; update the progress display
    ;; ========================================================================

    ;; This instruction is patched with different values at runtime.

    .db   LD_A_N
_digits:
    .db   0      ;; digits (BCD) for progress display while loading a snapshot

    inc   a
    daa
    push  af             ;; remember flags
    ld    (_digits), a
    ld    c, a
    jr    nz, not_100k   ;; turned from 99->100?

    ;; Number of kilobytes became zero in BCD:
    ;; means it just turned from 99 to 100.
    ;; Print the digit '1' for hundreds.

    ld    l, a
    inc   a      ;; A is now 1
    call  show_attr_digit
    ld    a, c

not_100k:
    pop   de             ;; recall flags, old F is now in E
    bit   #4, e          ;; was H flag set? Then the tens have increased
    jr    z, not_10k

    ;; Print tens (_x_)

    rra
    ld    l, #7
    call  show_attr_digit_already_shifted

not_10k:
    ;; Print single-number digit (__x)

    ld    a, c
    ld    l, #14
    call  show_attr_digit

    pop   de

    ;; ************************************************************************
    ;; update progress bar
    ;; ************************************************************************

    ld    hl, #_kilobytes_loaded
    inc   (hl)
    ld    a, (hl)

    ;; ------------------------------------------------------------------------
    ;; Scale loaded number of kilobytes to a value 0..32. By default, 48k
    ;; snapshots are scaled * 2 / 3. For 128k snapshots, the add below is
    ;; patched to a NOP, and the immediate constant for LD_B_N to 4.
    ;; ------------------------------------------------------------------------

progress_add_instr:
    add   a, a       ;; patched to NOP for 128k snapshots

    .db   LD_B_N
progress_ratio:
    .db   3          ;; patched to 4 for 128k snapshots

    call  a_div_b
    ld    a, c

00002$:
    or    a, a       ;; zero progress?
    ret   z
    ld    bc, #PROGRESS_BAR_BASE-1
    add   a, c
    ld    c, a
    ld    a, #(GREEN + (GREEN << 3))
    ld    (bc), a

    ;; ========================================================================
    ;; if all data has been loaded, perform the context switch
    ;; ========================================================================

    .db   LD_A_N
kilobytes_expected:
    .db   48         ;; initial assumption, possibly patched to 128 in s_header

    cp    a, (hl)
    ret   nz

    jp    context_switch             ;; in stage 1 loader (ROM)


;; ############################################################################
;; state CHUNK_REPCOUNT
;; ############################################################################

    .area _STAGE2

_s_chunk_repcount:

    ;;
    ;; An escape sequence has been detected:
    ;; patch code accordingly
    ;;

    ld   a, #JR_NZ
    ld   (repetition_branch), a

    call _get_next_byte
    ld   (_repcount), a

    call _dec_chunk_bytes

    ld    ix, #_s_chunk_repvalue

    ret

;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _STAGE2

_s_chunk_repvalue:

    call _get_next_byte
    ld   (_rep_value), a

    call _dec_chunk_bytes

    ld    ix, #s_chunk_write_data

    jp    s_chunk_write_data   ;; FIXME reorder routines


;; ############################################################################
;; z80_loader_receive_hook
;; ############################################################################

z80_loader_receive_hook:

    ;; ------------------------------------------------------------------------
    ;; register allocation (shared in all states):
    ;; IX: current state (pointer to function)
    ;; IY: read pointer (pointer to somewhere in rx_frame)
    ;; DE: write pointer (pointer to somewhere in RAM)
    ;; ------------------------------------------------------------------------

    .dw  LD_IX_NN            ;; LD IX, #nn
z80_loader_state:
    .dw  s_header            ;; initial state

    ld   iy, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, (_tftp_write_pos)

    ;; ------------------------------------------------------------------------
    ;; set up _received_data_length
    ;; ------------------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH)
    ld   a, l      ;; byteswap: length is stored in network order in UDP header
    ld   l, h
    ld   h, a
    ld   bc, #0x10000 - UDP_HEADER_SIZE - TFTP_HEADER_SIZE
    add  hl, bc
    ld   (_received_data_length), hl

    ;; ========================================================================
    ;; read bytes, evacuate when needed, call state functions
    ;; ========================================================================

receive_snapshot_byte_loop:

    ld   (z80_loader_state), ix
    ld   (_tftp_write_pos), de

    ;; ------------------------------------------------------------------------
    ;; if received_data_length is zero, we are done
    ;; ------------------------------------------------------------------------

    ld    hl, (_received_data_length)
    ld    a, h
    or    a, l
    ret   z

    ;; ------------------------------------------------------------------------
    ;; check evacuation status only if low byte of E is zero
    ;; ------------------------------------------------------------------------

    ld    a, e
    or    a, a
    jr    nz, receive_snapshot_no_evacuation

    ld    a, d     ;; A is now high byte of tftp_write_pos

    ;; ------------------------------------------------------------------------
    ;; reached RUNTIME_DATA (resident area)?
    ;; ------------------------------------------------------------------------

    ld    hl, #evacuation_activation_instr

    cp    a, #>RUNTIME_DATA
    jr    nz, receive_snapshot_not_entering_runtime_data

    ;; ------------------------------------------------------------------------
    ;; then store data in EVACUATION_TEMP_BUFFER instead,
    ;; and enable evacuation
    ;; ------------------------------------------------------------------------

    ld    d, #>EVACUATION_TEMP_BUFFER
    ld    (hl), #JR_NZ     ;; evacuation is now enabled

    jr    receive_snapshot_no_evacuation

receive_snapshot_not_entering_runtime_data:

    ;; ------------------------------------------------------------------------
    ;; is an evacuation about to be completed?
    ;; ------------------------------------------------------------------------

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
evacuation_activation_instr:
    jr    receive_snapshot_no_evacuation

    ;; ------------------------------------------------------------------------
    ;; then set _tftp_write_pos := RUNTIME_DATA + RUNTIME_DATA_LENGTH,
    ;; and disable evacuation
    ;; ------------------------------------------------------------------------

    ld    d, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH)

    ;; evacuation (soon) done: change JR above to skip evacuation next time
    ld    (hl), #JR_UNCONDITIONAL

    ;; ------------------------------------------------------------------------
    ;; prepare context switch and copy the evacuated data to ENC28J60 RAM
    ;; ------------------------------------------------------------------------

    call  prepare_context

receive_snapshot_no_evacuation:

    ;; ------------------------------------------------------------------------
    ;; call function pointed to by z80_loader_state
    ;; there is no "CALL (IX)" instruction, so CALL a JP (IX) instead
    ;; ------------------------------------------------------------------------

    call  jp_ix_instr

    jr    receive_snapshot_byte_loop

jp_ix_instr:
    jp    (ix)