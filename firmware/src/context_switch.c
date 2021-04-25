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
#include "syslog.h"
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
 *    ei / di       (depending on whether interrupts are to be enabled)
 *    jp  NN
 *
 * (IP configuration and state for registers BC, DE, HL, SP, F, R follow
 * in the remaining scan lines of this 5-cell trampoline)

 */
#define VRAM_TRAMPOLINE_START           0x4000
#define VRAM_TRAMPOLINE_OUT             (VRAM_TRAMPOLINE_START)
#define VRAM_TRAMPOLINE_LD_A            0x4100
#define VRAM_TRAMPOLINE_EIDI            0x4200
#define VRAM_TRAMPOLINE_JP_FINAL        0x4201

/* ------------------------------------------------------------------------ */

/*
 * Register state, stored in VRAM along with the trampoline
 */

#define VRAM_REGSTATE_PC                (VRAM_TRAMPOLINE_JP_FINAL + 1)

#define VRAM_REGSTATE_A                 (VRAM_TRAMPOLINE_LD_A + 1)

#define VRAM_REGSTATE_BC                0x4300
#define VRAM_REGSTATE_SP                0x4302
#define VRAM_REGSTATE_F                 0x4304

#define VRAM_REGSTATE_DE                0x4400
#define VRAM_REGSTATE_HL                0x4402
#define VRAM_REGSTATE_R                 0x4404

/*
 * IP configuration, stored in VRAM along with the trampoline
 */

#define VRAM_TRAMPOLINE_HOST_IP         0x4500
#define VRAM_TRAMPOLINE_BCAST_IP        0x4600
#define VRAM_TRAMPOLINE_TFTP_SERVER_IP  0x4700

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
#define REG_R_OFFSET                    (0xf8)

/* ------------------------------------------------------------------------ */

/*
 * Macros to store 8- or 16-bit values on absolute addresses
 */
#define STORE8(addr, value)         (*((uint8_t *)(addr)) = value)
#define STORE16(addr, value)        (*((uint16_t *)(addr)) = value)
#define STORE_IP(addr, value)       (*((ipv4_address_t *)(addr)) = value)

/* ========================================================================= */

#ifndef SB_MINIMAL
void
evacuate_data(void)
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
      inc  h
      ld   a, h
      cp   #0x42
    jr   nz, write_trampoline_loop

    ld   hl, #VRAM_TRAMPOLINE_OUT
    ld   (hl), #0xD3             ;;  *0x4000 = OUT(N),A
    inc  h
    ld   (hl), #0x3E             ;;  *0x4100 = LD A, #N
    inc  l
    inc  h
    ld   (hl), #0xC3             ;;  *0x4201 = JP NN
    dec  h
    dec  h
    ld   (hl), #SPI_OUT          ;;  *0x4001 = address for OUT above

  __endasm;

  STORE8(VRAM_TRAMPOLINE_EIDI,
         (snapshot_header.iff1) ? 0xFB : 0xF3);  /* ei / di */

  /*
   * Write register state to VRAM trampoline area
   */
  STORE8(VRAM_REGSTATE_A, snapshot_header.a);
  STORE8(VRAM_REGSTATE_F, snapshot_header.f);
  STORE8(VRAM_REGSTATE_R,
         ((snapshot_header.r + REG_R_OFFSET) & 0x7f)
          | ((snapshot_header.snapshot_flags & 0x01) << 7));
  STORE16(VRAM_REGSTATE_BC, snapshot_header.bc);
  STORE16(VRAM_REGSTATE_DE, snapshot_header.de);
  STORE16(VRAM_REGSTATE_HL, snapshot_header.hl);
  STORE16(VRAM_REGSTATE_SP, snapshot_header.sp);

  if (IS_EXTENDED_SNAPSHOT_HEADER(&snapshot_header)) {
    STORE16(VRAM_REGSTATE_PC, snapshot_header.extended_pc);
  }
  else {
    STORE16(VRAM_REGSTATE_PC, snapshot_header.pc);
    snapshot_header.hw_state_7ffd = 0x30 + DEFAULT_BANK; /* lock, ROM 1 */
  }

  /*
   * Write IP configuration to VRAM trampoline area
   */
  STORE_IP(VRAM_TRAMPOLINE_HOST_IP, ip_config.host_address);
  STORE_IP(VRAM_TRAMPOLINE_BCAST_IP, ip_config.broadcast_address);
  STORE_IP(VRAM_TRAMPOLINE_TFTP_SERVER_IP, ip_config.tftp_server_address);

  /* Write data to ENC28J60. */
  enc28j60_write_memory_at(ENC28J60_EVACUATED_DATA,
			   (uint8_t *) EVACUATION_TEMP_BUFFER,
			   RUNTIME_DATA_LENGTH);
}
#endif

/* ------------------------------------------------------------------------ */

/*
 * Restore system state using VRAM trampoline.
 */
void
context_switch(void)
{
  // set up ERDPT for reading from the ENC28J60 below (ENC28J60_READ_INLINE)
  enc28j60_write_register16(ERDPT, ENC28J60_EVACUATED_DATA);

  /*
   * Restore paging and sound register contents, if valid values exist
   */
  if (IS_EXTENDED_SNAPSHOT_HEADER(&snapshot_header)
      && IS_128K_MACHINE(snapshot_header.hw_type))
  {
    static __sfr __banked __at(0xfffd) register_select;
    static __sfr __banked __at(0xbffd) register_value;

    uint8_t reg;

    for (reg = 0; reg < 16; reg++) {
      register_select = reg;
      register_value  = snapshot_header.hw_state_snd[reg];
    }
    register_select = snapshot_header.hw_state_fffd;

    /*
     * For a plain 128k snapshot on a +2A/+3 machine, copy the ROM
     * selection bit to the +2A/+3 memory configuration
     */
    memcfg_plus((snapshot_header.hw_state_7ffd & MEMCFG_PLUS_ROM_HI) >> 2);
    memcfg(snapshot_header.hw_state_7ffd);
  } else {
    /*
     * For 48k snapshots, we still need to write something to the
     * paging registers
     *
     * (page in bank 0 at 0xc000, screen 0, 48k ROM, lock)
     */
    memcfg_plus(MEMCFG_PLUS_ROM_HI);
    memcfg(MEMCFG_ROM_LO + MEMCFG_LOCK + DEFAULT_BANK);
  }

  __asm

    ;; Restore border

    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS)
    rra
    and    a, #0x07
    out    (__ula_port), a

    ;; Restore interrupt mode

    di

    im     0   ;; start out with this as default, revise if needed
    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    and    a, #0x03
    jr     z, context_switch_im_done
    im     1
    cp     a, #1
    jr     z, context_switch_im_done
    im     2
context_switch_im_done::

    ;; Restore the following registers early,
    ;; so we can avoid using VRAM for them:
    ;; - alternate registers (BC, DE, HL, AF)
    ;; - IX & IY
    ;; - I (interrupt register)

    ld     bc, (_snapshot_header + Z80_HEADER_OFFSET_BC_P)
    ld     de, (_snapshot_header + Z80_HEADER_OFFSET_DE_P)
    ld     hl, (_snapshot_header + Z80_HEADER_OFFSET_HL_P)
    exx

    ld     hl, #_snapshot_header + Z80_HEADER_OFFSET_F_P
    ld     sp, hl
    pop    af
    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_A_P)
    ex     af, af'     ;; ' apostrophe for syntax coloring

    ld     ix, (_snapshot_header + Z80_HEADER_OFFSET_IX)
    ld     iy, (_snapshot_header + Z80_HEADER_OFFSET_IY)

    ld     a, (_snapshot_header + Z80_HEADER_OFFSET_I)
    ld     i, a

    ENC28J60_READ_INLINE(RUNTIME_DATA, RUNTIME_DATA_LENGTH)

    ;; Restore F

    ld    hl, #VRAM_REGSTATE_F
    ld    sp, hl
    pop   af

    ;; Restore BC, DE, HL & SP

    ld    bc, (VRAM_REGSTATE_BC)
    ld    de, (VRAM_REGSTATE_DE)
    ld    hl, (VRAM_REGSTATE_HL)
    ld    sp, (VRAM_REGSTATE_SP)

    ;; Restore R

    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;;
    ;; Set up final register state for trampoline
    ;;

    ld    a, #0x20      ;; page out SpeccyBoot, pull reset on ENC28J60 low

    jp    VRAM_TRAMPOLINE_START

    __endasm;
}
