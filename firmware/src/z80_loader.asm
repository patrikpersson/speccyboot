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
;; Offset to the R register when stored, to compensate for the fact that R
;; is affected by the execution of the trampoline.
;;
;; Calibrate this offset as follows:
;;
;; - Set it temporarily to 0, and run one of the test images
;; - Assume E = expected value of R        (0x2e in test image),
;;          N = actual value of R (as presented in binary by the test image)
;; - Set REG_R_ADJUSTMENT := (E - N)
;; ----------------------------------------------------------------------------

REG_R_ADJUSTMENT   = 0xEF

;; ============================================================================

    .area _DATA

_received_data:
    .ds   2       ;; pointer to received TFTP data

_received_data_length:
    .ds   2       ;; number of valid bytes remaining in received_data

_chunk_bytes_remaining:
    .ds   2       ;; bytes remaining to unpack in current chunk

;; ----------------------------------------------------------------------------
;; state for a repetition sequence
;; ----------------------------------------------------------------------------

_rep_count:        ;; set: chunk_compressed_repcount
    .ds   1        ;; read: chunk_compressed_repetition

_rep_value:
    .ds   1        ;; byte value for repetition

;; ----------------------------------------------------------------------------
;; expected and currently loaded no. of kilobytes, for progress display
;; ----------------------------------------------------------------------------

_kilobytes_expected:
     .ds   1
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
;; Destroys AF, DE, H; saves BC
;; ############################################################################

ROM_DIGITS = 0x3D00 + 16 * 8 + 1 ;; address of '0' bits, first actual scan line

show_attr_digit:

    add   a, a
    add   a, a
    add   a, a

show_attr_digit_already_shifted:  ;; special target for below

    and   a, #0x78           ;; binary 01111000
    ld    de, #ROM_DIGITS
    add   a, e      ;; because all digits are placed in a single 256b page
    ld    e, a

    ld    h, #>ATTR_DIGIT_ROW

    di    ;; SpeccyBoot is about to be paged out

    ld    a, #SPI_IDLE+SPI_CS+PAGE_OUT
    out   (SPI_OUT), a

show_attr_char_address_known:
    push  bc

    ld    c, #6
00001$:
    ld    a, (de)
    add   a, a
    inc   de
    ld    b, #6
00002$:
    add   a, a
    jr    nc, 00003$
    ld    (hl), #WHITE + (WHITE << 3)
    jr    00004$
00003$:
    ld    (hl), #BLACK + (BLACK << 3)
00004$:
    inc   hl
    djnz  00002$

    ld    a, #(ROW_LENGTH-6)
    add   a, l
    ld    l, a

    dec   c
    jr    nz, 00001$

    ld    a, #SPI_IDLE+SPI_CS       ;; page SpeccyBoot back in
    out   (SPI_OUT), a
    ei

    pop   bc
    ret


;; ############################################################################
;; evacuate_data
;;
;; Evacuate data from the temporary buffer to ENC28J60 SRAM. Examine the stored
;; .z80 header, and prepare the context switch to use information
;; (register state etc.) in it.
;; ############################################################################

evacuate_data:

    ;; ========================================================================
    ;; Clear out the top-left five character cells, by setting ink colour
    ;; to the same as the paper colour. Which colour is chosen depends on how
    ;; many pixels are set in that particular character cell.
    ;;
    ;; (These character cells are used as temporary storage for the trampoline
    ;; below.)
    ;; ========================================================================

    ld   bc, #EVACUATION_TEMP_BUFFER
    ld   hl, #BITMAP_BASE

    ld   d, #5
evacuate_data_loop1:   ;;  loop over character cells
      ld   e, #0       ;;  accumulated bit weight
      push bc

      ld   c, #8
evacuate_data_loop2:   ;;  loop over pixel rows in cell
        ld   a, (hl)
        inc  h

        ld   b, #8
evacuate_data_loop3:   ;;  loop over pixels in cell
          rra
          jr   nc, pixel_not_set
          inc  e
pixel_not_set:
        djnz evacuate_data_loop3

        dec  c
      jr   nz, evacuate_data_loop2

      ld   bc, #0x7ff  ;;  decrease for loop above + increase to next cell
      xor  a           ;;  clear C flag
      sbc  hl, bc
      pop  bc

      ld   a, e
      cp   #33         ;;  more than half of the total pixels in cell
      ld   a, (bc)
      jr   nc, evac_use_fg

      ;; few pixels set -- use background color

      rra
      rra
      rra

evac_use_fg:  ;; many pixels set -- use foreground color

      and  #7

      ld   e, a
      add  a, a
      add  a, a
      add  a, a
      or   a, e

evac_colour_set:
      ld   (bc), a
      inc  bc
      dec  d
    jr   nz, evacuate_data_loop1

    ;; ------------------------------------------------------------------------
    ;; write JP nn instructions to VRAM trampoline, at positions 0x40X2
    ;; ------------------------------------------------------------------------

    ld   h, #0x40
    ld   b, #6
write_trampoline_loop:
      ld   l, #2
      ld   (hl), #0xc3        ;; JP nn
      inc  hl
      ld   (hl), #0           ;; low byte of JP target is 0
      inc  hl
      ld   (hl), h
      inc  (hl)               ;; high byte of JP target
      inc  h
    djnz   write_trampoline_loop

    ;; ------------------------------------------------------------------------
    ;; write OUT(SPI_OUT), A to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #0xD3 + 0x100 * SPI_OUT    ;; *0x4000 = OUT(SPI_OUT), A
    ld   (VRAM_TRAMPOLINE_OUT), hl

    ;; ------------------------------------------------------------------------
    ;; write LD A, x to trampoline  (value to be stored in I)
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_I - 1)
    ld   l, #0x3E                  ;; LD A, n
    ld   (VRAM_TRAMPOLINE_LD_A_FOR_I), hl

    ;; ------------------------------------------------------------------------
    ;; write LD A, x to trampoline  (actual value for A)
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_A)
    ld   h, a
    ld   (VRAM_TRAMPOLINE_LD_A), hl

    ;; ------------------------------------------------------------------------
    ;; write LD I, A to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #0x47ED
    ld   (VRAM_TRAMPOLINE_LD_I), hl

    ;; ------------------------------------------------------------------------
    ;; write NOP and IM0/IM1/IM2 to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #VRAM_TRAMPOLINE_NOP
    ld   (hl), l                            ;; *0x4500 = NOP
    dec  h
    ld   (hl), #0xED                        ;; *0x04400 = first byte of IMx
    inc  l
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    ld   b, #0x46                           ;; second byte of IM0
    and  a, #3
    jr   z, im_set
    ld   b, #0x56                           ;; second byte of IM1
    dec  a
    jr   z, im_set
    ld   b, #0x5E                           ;; second byte of IM2
im_set:
    ld   (hl), b

    ;; ------------------------------------------------------------------------
    ;; write EI or NOP to trampoline, depending on IFF1 state in snapshot
    ;; ------------------------------------------------------------------------

    inc  h                                  ;; now back at 0x4501
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    jr   z, evacuate_di     ;; flag byte is zero, which also happens to be NOP
    ld   a, #0xFB           ;; EI
evacuate_di:
    ld   (hl), a

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_R)
    add  a, #REG_R_ADJUSTMENT
    and  a, #0x7f
    ld   b, a
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #0x01
    rrca
    or   a, b
    ld   (VRAM_REGSTATE_R), a

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_F
    ld   de, #VRAM_REGSTATE_F
    ld   bc, #5                  ;; F + BC + HL
    ldi

    ;; HL now points to _snapshot_header + Z80_HEADER_OFFSET_BC_HL
    ld   de, #VRAM_REGSTATE_BC_HL_F
    ldir

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ;; ========================================================================
    ;; set PC value in VRAM trampoline
    ;; ========================================================================

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, evacuate_pc

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+: use PC value from extended snapshot header,
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)

evacuate_pc:
    ld   (VRAM_REGSTATE_PC), hl

    ;; ========================================================================
    ;; write evacuated data to ENC28J60 RAM
    ;; ========================================================================

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR + (EWRPTL & REG_MASK)
    call enc28j60_write_register16

    ld   de, #RUNTIME_DATA_LENGTH
    ld   hl, #EVACUATION_TEMP_BUFFER

    jp   enc28j60_write_memory


;; ############################################################################
;; update_progress
;;
;; If the number of bytes loaded reached an even kilobyte,
;; increase kilobyte counter and update status display
;; ############################################################################

update_progress:

    ld   hl, (_tftp_write_pos)

    ;; check if HL is an integral number of kilobytes,
    ;; return early otherwise

    xor  a
    or   l
    ret  nz
    ld   a, h
    and  #0x03
    ret  nz

    ;; ========================================================================
    ;; update the progress display
    ;; ========================================================================

    ;; This instruction is patched with different values at runtime.
    ;; It is LD A, #n.

    .db   0x3e           ;; LD A, #n
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

    ld    l, a   ;; L is now 0
    inc   a      ;; A is now 1
    call  show_attr_digit
    ld    a, c

not_100k:
    pop   hl             ;; recall flags, old F is now in L
    bit   #4, l          ;; was H flag set? Then the tens have increased
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

    ;; ************************************************************************
    ;; update progress bar
    ;; ************************************************************************

    ld    hl, #_kilobytes_loaded
    inc   (hl)
    ld    a, (_kilobytes_expected)
    ld    d, a
    cp    a, #48     ;; 48k snapshot?
    ld    a, (hl)
    jr    z, 00003$  ;; 128k snapshot => progress = kilobytes / 4
    rra              ;; C is clear after CP above
    srl   a
    jr    00002$

00003$:              ;; 48k snapshot => progress = kilobytes * 2 / 3
    add   a, a
    ld    b, #3
    call  a_div_b
    ld    a, c

00002$:
    or    a, a       ;; zero progress?
    ret   z
    ld    bc, #PROGRESS_BAR_BASE-1
    add   a, c
    ld    c, a
    ld    a, #(WHITE + (WHITE << 3) + BRIGHT)
    ld    (bc), a

    ;; ========================================================================
    ;; if all data has been loaded, perform the context switch
    ;; ========================================================================

    ld    a, d
    cp    a, (hl)
    ret   nz

#ifdef PAINT_STACK
    di
    halt
#else
    jp    context_switch             ;; in stage 1 loader (ROM)
#endif

;; ############################################################################
;; _get_next_byte
;;
;; Returns *received_data++ in A
;; also decreases received_data_length
;;
;; (reads byte from received_data, increases received_data, returns byte in A)
;; Modifies HL (but not F)
;; ############################################################################

_get_next_byte:

    ld   hl, (_received_data)
    ld   a, (hl)
    inc  hl
    ld   (_received_data), hl

    ld   hl, (_received_data_length)
    dec  hl
    ld   (_received_data_length), hl

    ret

;; ############################################################################
;; _dec_chunk_bytes
;;
;; Decreases chunk_bytes_remaining (byte counter in compressed chunk)
;; ############################################################################

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
    ;; assume 48k snapshot, until more details are known
    ;; ------------------------------------------------------------------------

    ld   a, #48
    ld   (_kilobytes_expected), a

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

    ld   ix, #_s_chunk_uncompressed
    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK
    jr   z, s_header_set_state
    ld   ix, #_s_chunk_compressed
    jr   s_header_set_state

s_header_ext_hdr:

    ;; ------------------------------------------------------------------------
    ;; extended header: adjust expected no. of kilobytes for a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_HW_TYPE)
    cp    a, #SNAPSHOT_128K
    jr    c, s_header_not_128k
    ld    a, #128
    ld    (_kilobytes_expected), a

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

    ld   hl, (_received_data)
    add  hl, de
    ld   (_received_data), hl

    ld   hl, (_received_data_length)
    or   a, a            ;; clear C flag
    sbc  hl, de
    ld   (_received_data_length), hl

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until evacuate_data is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #_snapshot_header
    ld   bc, #Z80_HEADER_RESIDENT_SIZE
    ldir

    ;; ========================================================================
    ;; copy some of the context data immediately
    ;; (48k/128k flag, 128k memory + sound configuration, border colour,
    ;; registers DE, alternate AF+BC+DE+HL, IX, IY)
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; check snapshot version (is PC == 0?)
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, prepare_context_48k     ;; non-zero PC means version 1, always 48k

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+:
    ;; load HW_TYPE into L, and memory config into H
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)

    ;; ------------------------------------------------------------------------
    ;; Check HW_TYPE: only use 128k memory config from snapshot if this is
    ;; actually a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld   a, l
    cp   a, #SNAPSHOT_128K
    jr   nc, prepare_context_set_bank
prepare_context_48k:
    ld   hl, #(MEMCFG_ROM_48K + MEMCFG_LOCK) << 8  ;; config for a 48k snapshot
prepare_context_set_bank:
    ld   (context_128k_flag), hl

    ;; ------------------------------------------------------------------------
    ;; copy sound register data (may be invalid for a 48k snapshot,
    ;; but then it will not be used in the actual context switch)
    ;; ------------------------------------------------------------------------

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_SND
    ld   de, #context_snd_regs
    ld   bc, #16               ;; 16 sound registers
    ldir

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_FFFD)
    ld   (de), a
    inc  de                    ;; DE now points to context_border

    ;; ------------------------------------------------------------------------
    ;; clean up MISC_FLAGS, turn it into a value ready for OUT (0xFE), A
    ;; ------------------------------------------------------------------------

    ld   l, #<_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS
    ld   a, (hl)
    rra
    and  a, #0x07
    ld   (de), a
    inc  de

    ;; ------------------------------------------------------------------------
    ;; copy DE, alternate BC+DE+HL
    ;; ------------------------------------------------------------------------

    inc  hl    ;; now points to Z80_HEADER_OFFSET_DE
    ld   de, #context_registers
    ld   bc, #2*4  ;; DE, alternate BC+DE+HL
    ldir

    ;; ------------------------------------------------------------------------
    ;; swap A_P and F_P (to make simple POP in context switch possible)
    ;; ------------------------------------------------------------------------

    ld   c, #5    ;; B==0 here; need BC==4 after LDI, for LDIR below

    ld   a, (hl)
    inc  hl
    ldi
    ld   (de), a
    inc  de

    ;; ------------------------------------------------------------------------
    ;; copy IX, IY
    ;; ------------------------------------------------------------------------

    ldir

    ret


    .area _STAGE2

;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive low byte of chunk length
;; ############################################################################

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

    ;; Decide on a good value for tftp_write_pos; store in HL.

    ld   b, a    ;; useful extra copy of A

    ;;
    ;; Need to handle page 5 separately -- if we do not use the address range
    ;; 0x4000..0x7fff, the evacuation stuff will not work.
    ;;

    ld   h, #0x40
    cp   a, #8                       ;; means page 5 (0x4000..0x7fff)
    jr   z, s_chunk_header3_set_page

    ;;
    ;; Page 1 in a 48k snapshot points to 0x8000, but 128k snapshots are
    ;; different.
    ;;

    ld   h, #0x80
    cp   a, #4                       ;; means page 1 (0x8000..0xbfff)
    jr   nz, s_chunk_header3_default_page

    ld   a, c
    cp   a, #SNAPSHOT_128K    ;; 128k snapshot?
    jr   c, s_chunk_header3_set_page

s_chunk_header3_default_page:

    ld   h, #0xc0
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
    ld   l, #0
    ld   (_tftp_write_pos), hl

    ;; If chunk_bytes_remaining is 0xffff, length is 0x4000

    ld    ix, #_s_chunk_compressed        ;; tentative next state

    ld   hl, (_chunk_bytes_remaining)
    inc  h
    ret  nz
    inc  l
    ret  nz

    ld   h, #0x40    ;; HL is now 0x4000
    ld   (_chunk_bytes_remaining), hl

    ld    ix, #_s_chunk_uncompressed

    ret

;; ############################################################################
;; state CHUNK_UNCOMPRESSED
;; ############################################################################

_s_chunk_uncompressed:

  ;;
  ;; compute BC as minimum of
  ;; - distance to next kilobyte for tftp_write_pos
  ;; - received_data_length
  ;; - chunk_bytes_remaining
  ;;

  ld  hl, #_tftp_write_pos + 1
  ld  a, (hl)
  add #4            ;; round up to next 512-bytes boundary
  and #0xfc         ;; clears C flag, so sbc below works fine
  ld  h, a
  xor a
  ld  l, a
  ld  bc, (_tftp_write_pos)
  sbc hl, bc
  ld  b, h
  ld  c, l

  ;;
  ;; is received_data_length less than BC?
  ;; if it is, set BC to received_data_length
  ;;

  ld  hl, (_received_data_length)
  and a     ;; clear C flag for sbc below
  sbc hl, bc
  jr  nc, checked_length

  ld  bc, (_received_data_length)

checked_length:

  ;;
  ;; is chunk_bytes_remaining less than BC?
  ;; if it is, set BC to chunk_bytes_remaining
  ;;

  ld  hl, (_chunk_bytes_remaining)
  and a     ;; clear C flag for sbc below
  sbc hl, bc
  jr  nc, checked_chunk_length

  ld  bc, (_chunk_bytes_remaining)

checked_chunk_length:

  ;;
  ;; subtract BC from received_data_length and chunk_bytes_remaining
  ;;

  and a     ;; clear C flag for sbc below
  ld  hl, (_received_data_length)
  sbc hl, bc
  ld  (_received_data_length), hl

  ;;
  ;; subtract BC from chunk_bytes_remaining: if zero remains, set the next
  ;; state to s_chunk_header
  ;;

  ld  hl, (_chunk_bytes_remaining)
  sbc hl, bc
  ld  a, h
  or  l
  jr  nz, no_new_state

  ld  ix, #_s_chunk_header

no_new_state:
  ld  (_chunk_bytes_remaining), hl

  ;;
  ;; if BC is zero, skip copying and status display update
  ;;
  ld  a, b
  or  c
  ret z

  ;;
  ;; Copy the required amount of data
  ;;

  ld  hl, (_received_data)
  ld  de, (_tftp_write_pos)
  ldir
  ld  (_received_data), hl
  ld  (_tftp_write_pos), de

  ;;
  ;; update the status display, if needed
  ;;

  jp   update_progress


;; ############################################################################
;; state CHUNK_COMPRESSED
;; ############################################################################

_s_chunk_compressed:

  ld  bc, (_chunk_bytes_remaining)
  ld  de, (_received_data_length)
  ld  hl, (_tftp_write_pos)
  ld  iy, (_received_data)

s_chunk_compressed_loop:

  ;;
  ;; if chunk_bytes_remaining is zero, terminate loop and switch state
  ;;

  ld  a, b
  or  c
  jr  z, s_chunk_compressed_chunk_end

  ;;
  ;; if received_data_length is zero, terminate loop
  ;;

  ld  a, d
  or  e
  jp  z, s_chunk_compressed_write_back

  ;;
  ;; read a byte of input, increase read pointer,
  ;; decrease chunk_bytes_remaining and received_data_length
  ;;

  ld  a, (iy)
  inc iy
  dec bc
  dec de

  ;;
  ;; act on read data
  ;;

  cp  #Z80_ESCAPE
  jr  z, s_chunk_compressed_found_escape
  ld  (hl), a
  inc hl

  ;;
  ;; if HL is an integral number of kilobytes,
  ;; update the status display
  ;;

  xor a
  or  l
  jr  nz, s_chunk_compressed_loop
  ld  a, h
  and #0x03
  jr  nz, s_chunk_compressed_loop

  call s_chunk_compressed_write_back

  jp  update_progress

  ;;
  ;; reached end of chunk: switch state
  ;;

s_chunk_compressed_chunk_end:
  ld  ix, #_s_chunk_header
  jr  s_chunk_compressed_write_back

  ;;
  ;; found escape byte: switch state
  ;;

s_chunk_compressed_found_escape:
  ;;
  ;; optimization: if 3 bytes (or more) are available, and this is really
  ;; a repetition sequence, jump directly to s_chunk_repetition
  ;;

  ;; if bc < 3, goto s_chunk_compressed_no_opt
  ld  a, b
  or  a
  jr  nz, s_chunk_compressed_rept1
  ld  a, c
  cp  #3
  jr  c, s_chunk_compressed_no_opt

s_chunk_compressed_rept1:
  ;; if de < 3, goto s_chunk_compressed_no_opt
  ld  a, d
  or  a
  jr  nz, s_chunk_compressed_rept2
  ld  a, e
  cp  #3
  jr  c, s_chunk_compressed_no_opt

s_chunk_compressed_rept2:
  ld  a, (iy)       ;; peek, do not change counts
  cp  #Z80_ESCAPE
  jr  nz, s_chunk_compressed_no_opt

  ;;
  ;; the optimization is possible -- read the sequence data and jump
  ;; to the correct state
  ;;
  inc iy
  ld  a, (iy)
  inc iy
  ld  (_rep_count), a
  ld  a, (iy)
  inc iy
  ld  (_rep_value), a

  dec bc
  dec bc
  dec bc
  dec de
  dec de
  dec de

  call s_chunk_compressed_write_back

  ld  ix, #_s_chunk_repetition

jp_ix_instr:         ;; convenient CALL target
  jp  (ix)

s_chunk_compressed_no_opt:
  ;;
  ;; no direct jump to s_chunk_repetition was possible
  ;;

  ld  ix, #_s_chunk_compressed_escape

s_chunk_compressed_write_back:
  ld  (_chunk_bytes_remaining), bc
  ld  (_received_data_length), de
  ld  (_tftp_write_pos), hl
  ld  (_received_data), iy

  ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

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

    push  af

    ld    hl, (_tftp_write_pos)
    ld    (hl), #Z80_ESCAPE
    inc   hl
    ld    (_tftp_write_pos), hl
    call  update_progress

    pop   af

    ld    hl, (_tftp_write_pos)
    ld    (hl), a
    inc   hl
    ld    (_tftp_write_pos), hl
    call  update_progress

    ld    ix, #_s_chunk_compressed

    ret


;; ############################################################################
;; state CHUNK_REPCOUNT
;; ############################################################################

_s_chunk_repcount:

    call _get_next_byte
    ld   (_rep_count), a

    call _dec_chunk_bytes

    ld    ix, #_s_chunk_repvalue

    ret

;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

_s_chunk_repvalue:

    call _get_next_byte
    ld   (_rep_value), a

    call _dec_chunk_bytes

    ld    ix, #_s_chunk_repetition

    ret

;; ############################################################################
;; state CHUNK_REPETITION
;; ############################################################################

_s_chunk_repetition:

  ld  a, (_rep_count)
  ld  b, a                      ;; loop counter rep_count
  ld  hl, (_tftp_write_pos)
  ld  a, (_rep_value)
  ld  c, a

s_chunk_repetition_loop:
  ld  a, b
  or  a
  jr  z, s_chunk_repetition_write_back

  ld  (hl), c
  inc hl
  dec b

  ;;
  ;; if HL is an integral number of kilobytes,
  ;; update the status display
  ;;

  ld  a, l
  or  a
  jr  nz, s_chunk_repetition_loop
  ld  a, h
  and #0x03
  jr  nz, s_chunk_repetition_loop

  ld  a, b
  ld  (_rep_count), a
  ld  (_tftp_write_pos), hl

  jp  update_progress

s_chunk_repetition_write_back:
  ld  (_rep_count), a           ;; copied from b above
  ld  (_tftp_write_pos), hl

  ld    ix, #_s_chunk_compressed

  ret

;; ############################################################################
;; z80_loader_receive_hook
;; ############################################################################

z80_loader_receive_hook:

    ;; ------------------------------------------------------------------------
    ;; set up _received_data & _received_data_length
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   (_received_data), hl

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

    .db  0xdd, 0x21          ;; LD IX, #NN
z80_loader_state:
    .dw  s_header            ;; initial state

receive_snapshot_byte_loop:

    ld   (z80_loader_state), ix

    ;; ------------------------------------------------------------------------
    ;; if received_data_length is zero, we are done
    ;; ------------------------------------------------------------------------

    ld    hl, (_received_data_length)
    ld    a, h
    or    a, l
    ret   z

    ;; ------------------------------------------------------------------------
    ;; check evacuation status only if low byte of _tftp_write_pos is zero
    ;; ------------------------------------------------------------------------

    ld    hl, #_tftp_write_pos
    ld    a, (hl)
    or    a, a
    jr    nz, receive_snapshot_no_evacuation

    ;; ------------------------------------------------------------------------
    ;; reached RUNTIME_DATA (resident area)?
    ;; ------------------------------------------------------------------------

    ld    de, #evacuation_activation_instr

    inc   hl
    ld    a, (hl)
    cp    a, #>RUNTIME_DATA
    jr    nz, receive_snapshot_not_entering_runtime_data

    ;; ------------------------------------------------------------------------
    ;; then store data in EVACUATION_TEMP_BUFFER instead,
    ;; and enable evacuation
    ;; ------------------------------------------------------------------------

    ld    a, #>EVACUATION_TEMP_BUFFER
    ld    (hl), a
    ld    a, #0x20   ;; JR NZ means evacuation is now activated
    ld    (de), a

    jr    receive_snapshot_no_evacuation

receive_snapshot_not_entering_runtime_data:

    ;; ------------------------------------------------------------------------
    ;; is an evacuation about to be completed?
    ;; ------------------------------------------------------------------------

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
evacuation_activation_instr:
    jr    receive_snapshot_no_evacuation  ;; patched to JR NZ while evacuating

    ;; ------------------------------------------------------------------------
    ;; then set _tftp_write_pos := RUNTIME_DATA + RUNTIME_DATA_LENGTH,
    ;; and disable evacuation
    ;; ------------------------------------------------------------------------

    ld    a, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH)
    ld    (hl), a

    ;; evacuation (soon) done: change JR above to skip evacuation next time
    ld    a, #0x18            ;; unconditional JR
    ld    (de), a

    ;; ------------------------------------------------------------------------
    ;; copy the evacuated data to ENC28J60 RAM,
    ;; and make some preparations for context switch
    ;; ------------------------------------------------------------------------

    call  evacuate_data

receive_snapshot_no_evacuation:

    ;; ------------------------------------------------------------------------
    ;; call function pointed to by z80_loader_state
    ;; there is no "CALL (IX)" instruction, so CALL a JP (IX) instead
    ;; ------------------------------------------------------------------------

    call  jp_ix_instr

    jr    receive_snapshot_byte_loop
