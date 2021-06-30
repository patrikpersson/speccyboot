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

    .include "context_switch.inc"

    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "util.inc"
    .include "z80_loader.inc"

    .area _CODE

;; ============================================================================
;; The following code is copied to the five top-left character cells in VRAM
;; ============================================================================

trampoline_data:

    ;; 0x4000
    
    out (SPI_OUT), a
    jp  0x4100

    ;; 0x4100

    ld  a, #0              ;; immediate value written to trampoline above
    jp  0x4200

    ;; 0x4200

    ei                     ;; replaced with NOP if IFF1=0 in snapshot header
    .db JP_UNCONDITIONAL   ;; jump address written to trampoline above

    ;; the loop that copies the trampoline above will also copy the next
    ;; two bytes from ROM to VRAM

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

    ld   hl, (kilobytes_expected_and_memory_config)
    ld   bc, #MEMCFG_ADDR
    out  (c), h               ;; 128k memory config

    ld   a, l                 ;; 128k snapshot?
    add  a, a                 ;; if L is 128, this sets carry flag
    jr   nc, context_switch_48k_snapshot

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot: restore sound registers, in reverse order
    ;; (makes index equal to loop counter, and ensures HL points to
    ;; something useful afterwards)
    ;; ------------------------------------------------------------------------

breakpoint::

    ld   e, #15
    ld   hl, #stored_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_SND + 15
context_switch_snd_reg_loop:
    ld   b, #>SND_REG_SELECT
    out  (c), e
    ld   b, #>SND_REG_VALUE
    ld   a, (hl)
    dec  hl
    out  (c), a
    dec  e
    ld   a, e
    inc  a
    jr   nz, context_switch_snd_reg_loop

    ld   a, (hl)              ;; now points to Z80_HEADER_OFFSET_HW_STATE_FFFD
    ld   b, #>SND_REG_SELECT
    out  (c), a

context_switch_48k_snapshot:

    ;; ------------------------------------------------------------------------
    ;; Restore interrupt mode
    ;; ------------------------------------------------------------------------

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    im   1
    rra                              ;; bit 0 set means IM 1 (there is no IM 3)
    jr   c, context_switch_im_set
    im   2
    rra                              ;; bit 1 set means IM 2 (same reason)
    jr   c, context_switch_im_set
    im   0

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
    ;; initialize memory transfer from ENC28J60:
    ;;
    ;; write RBM opcode while the stack pointer is still valid
    ;; ------------------------------------------------------------------------

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

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
    ;; Restore application data temporarily stored in ENC28J60 RAM
    ;; (while not using the stack). RBM opcode already written above.
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; read RUNTIME_DATA_LENGTH bytes from current ERDPT to RUNTIME_DATA
    ;; ------------------------------------------------------------------------

    ld    hl, #RUNTIME_DATA
context_switch_restore_bytes_loop:

    ld    b, #8                      ;; one byte
context_switch_restore_bits_loop:
    SPI_READ_BIT_TO  (hl)
    djnz  context_switch_restore_bits_loop

    inc   hl
    ld    a, h
    cp    a, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH) ;; integral number of pages
    jr    nz, context_switch_restore_bytes_loop

    ;; ------------------------------------------------------------------------
    ;; we're done with the ENC28J60: put it in the reset state
    ;; ------------------------------------------------------------------------

    xor a, a
    out (SPI_OUT), a

    ;; ------------------------------------------------------------------------
    ;; Restore BC, HL, F
    ;; ------------------------------------------------------------------------

    ld    sp, #VRAM_REGSTATE_BC_HL_F
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

    ld    a, #0x20      ;; page out SpeccyBoot, keep ENC28J60 in reset

    jp    VRAM_TRAMPOLINE_START
