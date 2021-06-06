;;
;; Module context_switch:
;;
;; Protecting SpeccyBoot runtime data during snapshot loading, and switching to
;; the final Spectrum system state from header data.
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

    .module context_switch
    .optsdcc -mz80

    .include "context_switch.inc"

    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "util.inc"

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

REG_R_ADJUSTMENT   = 0xF7

;; ============================================================================

    .area _DATA

context_128k_flag:   ;; zero means 48k, non-zero means 128k
    .ds   2          ;; second byte is 128 memory configuration

stored_snapshot_header:
    .ds   Z80_HEADER_RESIDENT_SIZE


;; ############################################################################
;; prepare_context
;; ############################################################################

    .area _CODE

prepare_context:

    ;; ------------------------------------------------------------------------
    ;; use alternate BC, DE, HL for scratch here
    ;; ------------------------------------------------------------------------

    exx

    ;; ========================================================================
    ;; Prepare VRAM trampoline.
    ;;
    ;; Clear out the top-left five character cells, by setting ink colour
    ;; to the same as the paper colour.
    ;;
    ;; (These character cells are used as temporary storage for the trampoline
    ;; below.)
    ;; ========================================================================

    ld   hl, #EVACUATION_TEMP_BUFFER        ;; points to attribute data
    ld   b, #5
clear_cells_loop:
    ld   a, (hl)
    rra
    rra
    rra
    and  a, #7
    ld   c, a
    ld   a, (hl)
    and  a, #0xf8
    or   a, c
    ld   (hl), a
    inc  hl
    djnz clear_cells_loop

    ;; ------------------------------------------------------------------------
    ;; write JP nn instructions to VRAM trampoline, at positions 0x40X2
    ;; ------------------------------------------------------------------------

    ld   h, #>VRAM_TRAMPOLINE_OUT
    ld   b, #3
write_trampoline_loop:
      ld   l, #2
      ld   (hl), #JP_UNCONDITIONAL        ;; JP nn
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
    ;; write LD A, x to trampoline
    ;; ------------------------------------------------------------------------

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_A)
    ld   h, a
    ld   l, #LD_A_N
    ld   (VRAM_TRAMPOLINE_LD_A), hl

    ;; ------------------------------------------------------------------------
    ;; write NOP and either EI or NOP, depending on IFF1 state in snapshot
    ;; ------------------------------------------------------------------------

    ld   h, b                  ;; B is zero after DJNZ above:
    ld   l, b                  ;; set HL to two NOP instructions

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    jr   z, evacuate_no_ei
    ld   h, #EI
evacuate_no_ei:
    ld   (VRAM_TRAMPOLINE_NOP), hl

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_R)

    ;; now L holds low 7 bits of R, and bit 0 of H holds bit 7 of R

    ld   a, #REG_R_ADJUSTMENT
    add  a, l
    rla                ;; carry now in bit 0, but shifted again out soon
    rr   h
    rra
    ld   (VRAM_REGSTATE_R), a

    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_F
    ld   de, #VRAM_REGSTATE_F
    ld   bc, #5                  ;; F + BC + HL
    ldi

    ;; HL now points to stored_snapshot_header + Z80_HEADER_OFFSET_BC_HL
    ld   e, #<VRAM_REGSTATE_BC_HL_F
    ldir

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ;; ========================================================================
    ;; Set up 48k/128k flag, 128k memory configuration, PC value
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; check snapshot version (is PC == 0?)
    ;; set up 128k flag + memory config in DE, snapshot PC in HL
    ;; ------------------------------------------------------------------------

    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, prepare_context_48k     ;; non-zero PC means version 1, always 48k

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+:
    ;; load HW_TYPE into E, and memory config into D
    ;; ------------------------------------------------------------------------

    ld   de, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    ld   hl, (stored_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)

    ;; ------------------------------------------------------------------------
    ;; Check HW_TYPE: only use 128k memory config from snapshot if this is
    ;; actually a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld   a, e
    cp   a, #SNAPSHOT_128K
    jr   nc, prepare_context_set_bank
prepare_context_48k:
    ld   de, #(MEMCFG_ROM_48K + MEMCFG_LOCK) << 8  ;; config for a 48k snapshot
prepare_context_set_bank:
    ld   (VRAM_REGSTATE_PC), hl
    ld   (context_128k_flag), de

    ;; ========================================================================
    ;; write evacuated data to ENC28J60 RAM
    ;; ========================================================================

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR + (EWRPTL & REG_MASK)
    rst  enc28j60_write_register16

    ld   de, #RUNTIME_DATA_LENGTH
    ld   hl, #EVACUATION_TEMP_BUFFER

    call enc28j60_write_memory

    exx

    ret

;; ============================================================================

    .area _CODE

;; ############################################################################
;; context_switch
;; ############################################################################

context_switch:

    di

    ;; ------------------------------------------------------------------------
    ;; set ERDPT := ENC28J60_EVACUATED_DATA
    ;; ------------------------------------------------------------------------

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #OPCODE_WCR + (ERDPTL & REG_MASK)
    rst  enc28j60_write_register16

    ;; ------------------------------------------------------------------------
    ;; set up 128k memory configuration,
    ;; and check whether this is a 48k or 128k snapshot
    ;; ------------------------------------------------------------------------

    ld   hl, (context_128k_flag)
    ld   bc, #MEMCFG_ADDR
    out  (c), h               ;; next byte after HW_TYPE: 128k memory config

    ld   a, l                 ;; HW_TYPE
    or   a, a
    jr   z, context_switch_48k_snapshot

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot: restore sound registers
    ;; ------------------------------------------------------------------------

    ld   de, #16   ;; D := 0; E := 16
    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_SND
context_switch_snd_reg_loop:
    ld   b, #>SND_REG_SELECT
    out  (c), d
    ld   b, #>SND_REG_VALUE
    ld   a, (hl)
    inc  hl
    out  (c), a
    inc  d
    dec  e
    jr   nz, context_switch_snd_reg_loop

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_FFFD)
    ld   b, #>SND_REG_SELECT
    ld   a, (hl)
    out  (c), a

context_switch_48k_snapshot:

    ;; ------------------------------------------------------------------------
    ;; Restore interrupt mode
    ;; ------------------------------------------------------------------------

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    im   0
    and  a, #3
    jr   z, context_switch_im_set
    im   1
    dec  a
    jr   z, context_switch_im_set
    im   2
context_switch_im_set:

    ;; ------------------------------------------------------------------------
    ;; Restore border
    ;; ------------------------------------------------------------------------

    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS
    ld   a, (hl)
    rra
    and  a, #0x07
    out  (ULA_PORT), a

    ;; ------------------------------------------------------------------------
    ;; Restore the following registers early,
    ;; so we can avoid using VRAM for them:
    ;; - DE
    ;; - alternate registers (BC, DE, HL, AF)
    ;; - IX & IY
    ;; - I
    ;; ------------------------------------------------------------------------

    inc    hl              ;; stored_snapshot_header + Z80_HEADER_OFFSET_DE
    ld     sp, hl

    pop    de
    exx
    pop    bc
    pop    de
    pop    hl
    exx

    ;; the .z80 snapshot format has switched A and F around, so some
    ;; trickery is required to restore AF'

    ;; A goes first (loaded temporarily to C)
    pop    bc

    ;; then F
    dec    sp
    pop    af
    dec    sp

    ld     a, c
    ex     af, af'

    pop    iy
    pop    ix

    ld    a, (stored_snapshot_header + Z80_HEADER_OFFSET_I)
    ld    i, a

    ;; ========================================================================
    ;; restore application data temporarily stored in ENC28J60 RAM
    ;; (while not using the stack)
    ;; ========================================================================

    ld     bc, #0x0800 + OPCODE_RBM        ;; 8 bits, opcode RBM
context_switch_restore_rbm_loop:
    spi_write_bit_from_c                   ;; avoid using stack
    djnz  context_switch_restore_rbm_loop

    ;; ------------------------------------------------------------------------
    ;; read RUNTIME_DATA_LENGTH bytes from current ERDPT to RUNTIME_DATA
    ;; ------------------------------------------------------------------------

    ld    hl, #RUNTIME_DATA
context_switch_restore_bytes_loop:

    ld    b, #8                      ;; one byte
context_switch_restore_bits_loop:
    spi_read_bit_to_c
    djnz  context_switch_restore_bits_loop

    ld    (hl), c
    inc   hl
    ld    a, h
    cp    a, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH) ;; integral number of pages
    jr    nz, context_switch_restore_bytes_loop

    ;; ------------------------------------------------------------------------
    ;; end SPI transaction
    ;; ------------------------------------------------------------------------

    ld  a, #SPI_IDLE
    out (SPI_OUT), a
    ld  a, #SPI_IDLE+SPI_CS
    out (SPI_OUT), a

    ;; ------------------------------------------------------------------------
    ;; Restore BC, HL, F
    ;; ------------------------------------------------------------------------

    ld    hl, #VRAM_REGSTATE_BC_HL_F
    ld    sp, hl
    pop   bc
    pop   hl
    pop   af        ;; A gets wrong value here, but this is fixed in trampoline

    ;; ------------------------------------------------------------------------
    ;; Restore SP and R
    ;; ------------------------------------------------------------------------

    ld    sp, (VRAM_REGSTATE_SP)

    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; Set up final register state for trampoline
    ;; ------------------------------------------------------------------------

    ld    a, #0x20      ;; page out SpeccyBoot, pull reset on ENC28J60 low

    jp    VRAM_TRAMPOLINE_START
