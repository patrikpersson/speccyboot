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

    .include "include/context_switch.inc"

    .include "include/enc28j60.inc"
    .include "include/eth.inc"
    .include "include/globals.inc"
    .include "include/spi.inc"
    .include "include/ui.inc"
    .include "include/util.inc"

;; ============================================================================

    .area _DATA

context_128k_flag:   ;; zero means 48k, non-zero means 128k
    .ds   1

context_7ffd:        ;; 128k memory config (I/O 0x7ffd)
    .ds   1

context_snd_regs:    ;; 16 bytes of sound register values (128k snapshots only)
    .ds   16

context_fffd:        ;; value for I/O 0xfffd  (sound register select)
    .ds   1

context_border:      ;; value for I/O 0xfe (border)
    .ds   1

context_registers:   ;; registers DE, BC', DE', HL', AF', IX, IY
    .ds   14

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
    call enc28j60_write_register16

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
    ld   hl, #context_snd_regs
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

    ld   b, #>SND_REG_SELECT
    ld   a, (hl)
    out  (c), a

context_switch_48k_snapshot:

    ld     hl, #context_border

    ;; ------------------------------------------------------------------------
    ;; Restore border
    ;; ------------------------------------------------------------------------

    ld     a, (hl)
    out    (ULA_PORT), a

    ;; ------------------------------------------------------------------------
    ;; Restore the following registers early,
    ;; so we can avoid using VRAM for them:
    ;; - DE
    ;; - alternate registers (BC, DE, HL, AF)
    ;; - IX & IY
    ;; ------------------------------------------------------------------------

    inc    hl
    ld     sp, hl
    pop    de
    exx
    pop    bc
    pop    de
    pop    hl
    exx

    pop    af
    ex     af, af'               ;;'

    pop     iy
    pop     ix

    ;; ========================================================================
    ;; restore application data temporarily stored in ENC28J60 RAM
    ;; ========================================================================

    ld     bc, #0x0800 + OPCODE_RBM        ;; 8 bits, opcode RBM
context_switch_restore_rbm_loop:
    spi_write_bit_from_c
    djnz  context_switch_restore_rbm_loop

    ;; ------------------------------------------------------------------------
    ;; read RUNTIME_DATA_LENGTH bytes from current ERDPT to RUNTIME_DATA
    ;; ------------------------------------------------------------------------

    ld    hl, #(RUNTIME_DATA)
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
    ;; Restore SP & R
    ;; ------------------------------------------------------------------------

    ld    sp, (VRAM_REGSTATE_SP)

    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; Set up final register state for trampoline
    ;; ------------------------------------------------------------------------

    ld    a, #0x20      ;; page out SpeccyBoot, pull reset on ENC28J60 low

    jp    VRAM_TRAMPOLINE_START
