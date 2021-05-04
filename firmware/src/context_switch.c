/*
 * Module context_switch:
 *
 * Protecting SpeccyBoot runtime data during snapshot loading, and switching to
 * the final Spectrum system state from header data.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "context_switch.h"

#include "enc28j60.h"
#include "globals.h"
#include "ui.h"

/* ========================================================================= */

/*
 * VRAM trampoline layout. Split onto multiple pixel lines, to reduce the
 * number of distorted character cells to 5.
 *
 * 0x4000:
 *    out (0x9f), a
 *    jp  0x4100
 * 0x4100:
 *    ld  a, #N
 *    jp  0x4200
 * 0x4200:
 *    im0/im1/im2   (depending on snapshot interrupt mode)
 *    jp  0x4300
 * 0x4300:
 *    nop           (for symmetry of JPs)
 *    ei / di       (depending on whether interrupts are to be enabled)
 *    jp  NN
 *
 * (state for registers BC, DE, HL, SP, F, R follow
 * in the remaining scan lines of this 5-cell trampoline)
 */
#define VRAM_TRAMPOLINE_START           0x4000
#define VRAM_TRAMPOLINE_OUT             (VRAM_TRAMPOLINE_START)
#define VRAM_TRAMPOLINE_LD_A            0x4100
#define VRAM_TRAMPOLINE_IM_2ND_BYTE     0x4201
#define VRAM_TRAMPOLINE_NOP             0x4300
#define VRAM_TRAMPOLINE_EIDI            0x4301
#define VRAM_TRAMPOLINE_JP_FINAL        0x4302

/* ------------------------------------------------------------------------ */

/*
 * Register state, stored in VRAM along with the trampoline
 */

#define VRAM_REGSTATE_PC                (VRAM_TRAMPOLINE_JP_FINAL + 1)

#define VRAM_REGSTATE_A                 (VRAM_TRAMPOLINE_LD_A + 1)

#define VRAM_REGSTATE_F_BC_HL           0x4400

#define VRAM_REGSTATE_SP                0x4500
#define VRAM_REGSTATE_DE                0x4502
#define VRAM_REGSTATE_R                 0x4504

/* ------------------------------------------------------------------------ */

/*
 * Offset to the R register when stored, to compensate for the fact that R
 * is affected by the execution of the trampoline.
 *
 * Calibrate this offset as follows:
 *
 * - Set it temporarily to 0, and run one of the test images
 * - Assume E = expected value of R        (0x2e in test image),
 *          N = actual value of R (as presented in binary by the test image)
 * - Set REG_R_OFFSET := (E - N)
 */
#define REG_R_OFFSET                    (0xf4)

/* ========================================================================= */

#ifndef SB_MINIMAL
void
evacuate_data(void)
__naked
{
  /*
   * Clear out the top-left five character cells, by setting ink colour
   * to the same as the paper colour. Which colour is chosen depends on how
   * many pixels are set in that particular character cell.
   *
   * (These character cells are used as temporary storage for the trampoline
   * below.)
   */
  __asm

    ld   bc, #EVACUATION_TEMP_BUFFER
    ld   hl, #BITMAP_BASE

    ld   d, #5
evacuate_data_loop1::  ;;  loop over character cells
      ld   e, #0       ;;  accumulated bit weight
      push bc

      ld   c, #8
evacuate_data_loop2::  ;;  loop over pixel rows in cell
        ld   a, (hl)
        inc  h

        ld   b, #8
evacuate_data_loop3::  ;;  loop over pixels in cell
          rra
          jr   nc, pixel_not_set
          inc  e
pixel_not_set::
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

evac_use_fg::  ;; many pixels set -- use foreground color

      and  #7

      ld   e, a
      add  a, a
      add  a, a
      add  a, a
      or   a, e

evac_colour_set::
      ld   (bc), a
      inc  bc
      dec  d
    jr   nz, evacuate_data_loop1

    ;;  write parts of trampoline code to VRAM (remainder done in C below)

    ld   h, #0x40
write_trampoline_loop::
      ld   l, #2
      ld   (hl), #0xc3
      inc  hl
      ld   (hl), #0
      inc  hl
      ld   a, h
      inc  a
      ld   (hl), a
      ld   h, a
      cp   #0x44
    jr   nz, write_trampoline_loop

    ld   hl, #0xD3 + 0x100 * SPI_OUT    ;; *0x4000 = OUT(SPI_OUT), A
    ld   (VRAM_TRAMPOLINE_OUT), hl

    ld   hl, #VRAM_TRAMPOLINE_LD_A
    ld   (hl), #0x3E                    ;;  *0x4100 = LD A, #N
    inc  h
    ld   (hl), #0xED                    ;;  *0x4200 = first byte of IM x
    inc  h
    ld   (hl), #0                       ;;  *0x4300 = NOP

    ;; ------------------------------------------------------------------------
    ;; write IM0/IM1/IM2 to trampoline
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    ld   hl, #VRAM_TRAMPOLINE_IM_2ND_BYTE
    ld   b, #0x46   ;; second byte of IM0
    and  a, #3
    jr   z, im_set
    ld   b, #0x56   ;; second byte of IM1
    dec  a
    jr   z, im_set
    ld   b, #0x5E   ;; second byte of IM2
im_set::
    ld   (hl), b

    ;; ------------------------------------------------------------------------
    ;; write EI or DI to trampoline, depending on IFF1 state in snapshot
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    ld   a, #0xFB           ;; EI
    jr   nz, evacuate_ei
    ld   a, #0xF3           ;; DI
evacuate_ei::
    ld   (VRAM_TRAMPOLINE_EIDI), a

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_A)
    ld   (VRAM_REGSTATE_A), a

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_R)
    add  a, #REG_R_OFFSET
    and  a, #0x7f
    ld   b, a
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #0x01
    rrca
    or   a, b
    ld   (VRAM_REGSTATE_R), a

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_F_BC_HL
    ld   de, #VRAM_REGSTATE_F_BC_HL
    ld   bc, #5                  ;; F + BC + HL
    ldir

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_DE)
    ld   (VRAM_REGSTATE_DE), hl

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, evacuate_pc_z80v1

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+: use PC value from extended snapshot header
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)
    jr   evacuate_pc
evacuate_pc_z80v1::

    ;; ------------------------------------------------------------------------
    ;; snapshot version 1: use PC value from regular snapshot header,
    ;; and set 128k bank configuration to 48k mode (locked banks, ROM1)
    ;; ------------------------------------------------------------------------

    ld   a, #0x30 + DEFAULT_BANK
    ld   (_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_7FFD), a
evacuate_pc::
    ld   (VRAM_REGSTATE_PC), hl

    ;; ------------------------------------------------------------------------
    ;; write evacuated data to ENC28J60 RAM
    ;; ------------------------------------------------------------------------

    ld   hl, #ENC28J60_EVACUATED_DATA
    push hl
    ld   hl, #ENC_OPCODE_WCR(EWRPTL) + 0x0100 * ENC_OPCODE_WCR(EWRPTH)
    push hl
    call _enc28j60_write_register16_impl
    pop  hl
    pop  hl

    ld   bc, #RUNTIME_DATA_LENGTH
    push bc
    ld   hl, #EVACUATION_TEMP_BUFFER
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  hl

    ret

  __endasm;
}
#endif

/* ------------------------------------------------------------------------ */

/*
 * Restore system state using VRAM trampoline.
 */
void
context_switch(void)
__naked
{
  __asm

    di

    ;; ------------------------------------------------------------------------
    ;; set ERDPT := ENC28J60_EVACUATED_DATA
    ;; ------------------------------------------------------------------------

    ld   hl, #ENC28J60_EVACUATED_DATA
    push hl
    ld   hl, #ENC_OPCODE_WCR(ERDPTL) + 0x0100 * ENC_OPCODE_WCR(ERDPTH)
    push hl
    call _enc28j60_write_register16_impl
    pop  hl
    pop  hl

    ;; ------------------------------------------------------------------------
    ;; check whether this is a 48k or 128k snapshot
    ;; ------------------------------------------------------------------------

    ;; 128k snapshots are only supported with .z80 versions with extended
    ;; header, indicated by PC==0

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l

    ;; ------------------------------------------------------------------------
    ;; For 48k snapshots, we still need to write something to the paging
    ;; register (page in bank 0 at 0xc000, screen 0, 48k ROM, lock)
    ;; ------------------------------------------------------------------------

    ld   a, #MEMCFG_ROM_LO + MEMCFG_LOCK + DEFAULT_BANK
    jr   nz, context_switch_48k_snapshot ;; no extended header, so no 128k snapshot

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    cp   a, #HW_TYPE_SPECTRUM_128K

    ld   a, #MEMCFG_ROM_LO + MEMCFG_LOCK + DEFAULT_BANK
    jr   c, context_switch_48k_snapshot

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot: restore sound registers
    ;; ------------------------------------------------------------------------

    ld   de, #16   ;; D := 0; E := 16
    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_SND
context_switch_snd_reg_loop::
    ld   bc, #SND_REG_SELECT
    out  (c), d
    ld   b, #>SND_REG_VALUE
    ld   a, (hl)
    inc  hl
    out  (c), a
    inc  d
    dec  e
    jr   nz, context_switch_snd_reg_loop

    ld   b, #>SND_REG_SELECT
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_FFFD)
    out  (c), a

    ;; ------------------------------------------------------------------------
    ;; 128k snapshot: configure memory banks
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_STATE_7FFD)

context_switch_48k_snapshot::

    ld   bc, #MEMCFG_ADDR
    out  (c), a

    ;; ------------------------------------------------------------------------
    ;; Restore border
    ;; ------------------------------------------------------------------------

    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS)
    rra
    and    a, #0x07
    out    (ULA_PORT), a

    ;; ------------------------------------------------------------------------
    ;; Restore the following registers early,
    ;; so we can avoid using VRAM for them:
    ;; - alternate registers (BC, DE, HL, AF)
    ;; - IX & IY
    ;; - I (interrupt register)
    ;; ------------------------------------------------------------------------

    ld     hl, #_snapshot_header + Z80_HEADER_OFFSET_BC_P
    ld     sp, hl
    pop    bc
    pop    de
    pop    hl
    exx

    ;; silly .z80 snapshot header has switched A and F,
    ;; so some stack juggling is required

    dec    sp
    pop    af      ;; now we got A, but not F
    ld     b, a
    pop    af      ;; now we got F
    dec    sp
    ld     a, b
    ex     af, af'     ;; ' apostrophe for syntax

    pop     iy
    pop     ix

    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_I)
    ld     i, a

    ENC28J60_READ_INLINE(RUNTIME_DATA, RUNTIME_DATA_LENGTH)

    ;; ------------------------------------------------------------------------
    ;; Restore F, BC, HL
    ;; ------------------------------------------------------------------------

    ld    hl, #VRAM_REGSTATE_F_BC_HL
    ld    sp, hl
    pop   af
    dec   sp
    pop   bc
    pop   hl

    ;; ------------------------------------------------------------------------
    ;; Restore DE & SP
    ;; ------------------------------------------------------------------------

    ld    de, (VRAM_REGSTATE_DE)
    ld    sp, (VRAM_REGSTATE_SP)

    ;; ------------------------------------------------------------------------
    ;; Restore R
    ;; ------------------------------------------------------------------------

    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; Set up final register state for trampoline
    ;; ------------------------------------------------------------------------

    ld    a, #0x20      ;; page out SpeccyBoot, pull reset on ENC28J60 low

    jp    VRAM_TRAMPOLINE_START

    __endasm;
}
