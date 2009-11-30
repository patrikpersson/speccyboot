/*
 * Module context_switch:
 *
 * Protecting SpeccyBoot runtime data during snapshot loading, and switching to
 * the final Spectrum system state from header data.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
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
#include "rxbuffer.h"

/* ========================================================================= */

/*
 * VRAM trampoline layout. Split onto multiple pixel lines, to reduce the
 * number of distorted character cells to 5.
 *
 * 0x3ffe:
 *    out (c), a    (in crt0.asm)
 * 0x4000:
 *    ld  a, #N
 *    jp  0x4100
 * 0x4100:
 *    ld  b, #N
 *    jp  0x4200
 * 0x4200:
 *    ld  c, #N
 *    jp  0x4300
 * 0x4300:
 *    ei / di       (depending on whether interrupts are to be enabled)
 *    jp  NN
 *
 * (register state other than A/B/C/IFF1 follows in the remaining scan lines
 * of this 5-cell trampoline)
 *
 * Entry point is 0x3FFE.
 */
#define VRAM_TRAMPOLINE_START           0x3ffe

#define VRAM_TRAMPOLINE_LD_A            0x4000
#define VRAM_TRAMPOLINE_LD_B            0x4100
#define VRAM_TRAMPOLINE_LD_C            0x4200

#define VRAM_TRAMPOLINE_EIDI            0x4300
#define VRAM_TRAMPOLINE_JP_FINAL        0x4301

/* ------------------------------------------------------------------------ */

/*
 * Register state, stored in VRAM along with the trampoline
 */

#define VRAM_REGSTATE_PC                (VRAM_TRAMPOLINE_JP_FINAL + 1)

#define VRAM_REGSTATE_A                 (VRAM_TRAMPOLINE_LD_A + 1)
#define VRAM_REGSTATE_F                 0x4304

#define VRAM_REGSTATE_B                 (VRAM_TRAMPOLINE_LD_B + 1)
#define VRAM_REGSTATE_C                 (VRAM_TRAMPOLINE_LD_C + 1)

#define VRAM_REGSTATE_DE                0x4400
#define VRAM_REGSTATE_HL                0x4402
#define VRAM_REGSTATE_I                 0x4404

#define VRAM_REGSTATE_SP                0x4500
#define VRAM_REGSTATE_FP                0x4502
#define VRAM_REGSTATE_AP                0x4503
#define VRAM_REGSTATE_BP                0x4504

#define VRAM_REGSTATE_CP                0x4600
#define VRAM_REGSTATE_DEP               0x4601
#define VRAM_REGSTATE_HLP               0x4603

#define VRAM_REGSTATE_IX                0x4700
#define VRAM_REGSTATE_IY                0x4702
#define VRAM_REGSTATE_R                 0x4704

/* ------------------------------------------------------------------------ */

/*
 * Offset to the R register when stored, to compensate for the fact that R
 * is affected by the execution of the trampoline.
 *
 * Calibrate this offset as follows:
 *
 * - Set it temporarily to 0, and run one of the test images
 * - Assume E = expected value of R,
 *          N = actual value of R (as presented in binary by the test image)
 * - Set REG_R_OFFSET := (E - N)
 */
#define REG_R_OFFSET                    (0xED)

/* ------------------------------------------------------------------------ */

/*
 * Macros to store 8- or 16-bit values on absolute addresses
 */
#define STORE8(addr, value)         (*((uint8_t *)(addr)) = value)
#define STORE16(addr, value)        (*((uint16_t *)(addr)) = value)

/* ========================================================================= */

/*
 * Snapshot header, temporarily stored here
 */
struct z80_snapshot_header_t snapshot_header;

/* ------------------------------------------------------------------------ */

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
  uint8_t        *attr_ptr  = (uint8_t *) EVACUATION_TEMP_BUFFER;
  const uint8_t *bitmap_ptr = (uint8_t *) BITMAP_BASE;
  uint8_t i;
  
  for (i = 0; i < 5; i++) {
    uint8_t attr_value = *attr_ptr;
    
    /*
     * Compute bit-weight of this character cell (number of set pixels)
     */
    uint8_t weight = 0;
    uint8_t j;
    for (j = 0; j < 8; j++) {
      uint8_t bitmap = *bitmap_ptr;
      uint8_t k;
      for (k = 0; k < 8; k++) {
        weight += (bitmap & 0x01);
        bitmap >>= 1;
      }
      bitmap_ptr += 0x0100;
    }
    bitmap_ptr -= 0x07ff; /* decrease for loop above + increase to next cell */
    
    if (weight <= 32) {
      /*
       * Few pixels set: use background colour
       */
      uint8_t paper_colour = (attr_value & 0x38) >> 3;      
      *attr_ptr++ = (attr_value & 0xf8) | paper_colour;
    }
    else {
      /*
       * Many pixels set: use foreground colour
       */
      uint8_t ink_colour = (attr_value & 0x07);
      *attr_ptr++ = (attr_value & 0xc7) | (ink_colour << 3);
    }
  }
  
  /*
   * Write trampoline code to VRAM
   */
  for (i = 0; i < 3; i++) {
    uint8_t *p = (uint8_t *)(0x4002 + i * 0x0100);
    *p++ = 0xC3;
    *p++ = 0x00;
    *p   = 0x41 + i;
  }

  STORE8(VRAM_TRAMPOLINE_LD_A,          0x3E);          /* ld a, #N */
  STORE8(VRAM_TRAMPOLINE_LD_B,          0x06);          /* ld b, #N */
  STORE8(VRAM_TRAMPOLINE_LD_C,          0x0E);          /* ld c, #N */
  STORE8(VRAM_TRAMPOLINE_JP_FINAL,      0xC3);          /* jp NN */

  STORE8(VRAM_TRAMPOLINE_EIDI,
         (snapshot_header.iff1) ? 0xFB : 0xF3);  /* ei / di */

  /*
   * Write register state to VRAM trampoline area
   */
  STORE8(VRAM_REGSTATE_A, snapshot_header.a);
  STORE8(VRAM_REGSTATE_F, snapshot_header.f);
  STORE8(VRAM_REGSTATE_I, snapshot_header.i);
  STORE8(VRAM_REGSTATE_R,
         ((snapshot_header.r + REG_R_OFFSET) & 0x7f)
          | ((snapshot_header.snapshot_flags & 0x01) << 7));
  STORE8(VRAM_REGSTATE_B, snapshot_header.b);
  STORE8(VRAM_REGSTATE_C, snapshot_header.c);
  STORE16(VRAM_REGSTATE_DE, snapshot_header.de);
  STORE16(VRAM_REGSTATE_HL, snapshot_header.hl);
  STORE16(VRAM_REGSTATE_SP, snapshot_header.sp);

  STORE8(VRAM_REGSTATE_AP, snapshot_header.a_p);
  STORE8(VRAM_REGSTATE_FP, snapshot_header.f_p);
  STORE8(VRAM_REGSTATE_BP, snapshot_header.b_p);
  STORE8(VRAM_REGSTATE_CP, snapshot_header.c_p);
  STORE16(VRAM_REGSTATE_DEP, snapshot_header.de_p);
  STORE16(VRAM_REGSTATE_HLP, snapshot_header.hl_p);
  STORE16(VRAM_REGSTATE_IX, snapshot_header.ix);
  STORE16(VRAM_REGSTATE_IY, snapshot_header.iy);

  if (IS_EXTENDED_SNAPSHOT_HEADER(&snapshot_header)) {
    STORE16(VRAM_REGSTATE_PC, snapshot_header.extended_pc);
  }
  else {
    STORE16(VRAM_REGSTATE_PC, snapshot_header.pc);
    snapshot_header.hw_state_7ffd = 0x30 + DEFAULT_BANK; /* lock, ROM 1 */
  }
  
  /*
   * Write data to ENC28J60
   */
  enc28j60_write_memory_at(EVACUATED_DATA,
                           (uint8_t *) EVACUATION_TEMP_BUFFER,
                           RUNTIME_DATA_LENGTH);
}

/* ------------------------------------------------------------------------ */

/*
 * Restore system state using VRAM trampoline.
 */
void
context_switch(void)
{
  /*
   * Send a notification using syslog, along with stack stack results
   */
  uint16_t stack_remaining = 0;
  const uint8_t *stack_low = &stack_bottom;
  while (stack_low[stack_remaining++] == 0xA8)
    ;
  syslog("snapshot loaded, executing (0x% bytes stack left)", stack_remaining);

  /*
   * Disable interrupts early: avoid nasty incidents when we fiddle with the
   * interrupt mode below
   */
  __asm   di  __endasm;
  
  /*
   * Restore paging and sound register contents, if valid values exist
   */
  if (IS_EXTENDED_SNAPSHOT_HEADER(&snapshot_header)
      && IS_128K_MACHINE(snapshot_header.hw_type))
  {
    static sfr banked at(0xfffd) register_select;
    static sfr banked at(0xbffd) register_value;
    
    uint8_t reg;

    for (reg = 0; reg < 16; reg++) {
      register_select = reg;
      register_value  = snapshot_header.hw_state_snd[reg];
    }
    register_select = snapshot_header.hw_state_fffd;
    select_bank(snapshot_header.hw_state_7ffd);
  }
  else {
    /*
     * For 48k machines, we still need to write something to the
     * paging register
     *
     * (page in bank 0 at 0xc000, screen 0, 48k ROM (ROM1), lock)
     */
    select_bank(0x30 + DEFAULT_BANK);
  }
  
  /*
   * Restore border colour
   */
  set_border((snapshot_header.snapshot_flags & 0x0e) >> 1);
  
  /*
   * Restore interrupt mode
   */
  switch (snapshot_header.int_mode & 0x03) {
    case 0:
      __asm   im 0  __endasm;
      break;
    case 1:
      __asm   im 1  __endasm;
      break;
    case 2:
      __asm   im 2  __endasm;
      break;
  }
  
  enc28j60_select_bank(BANK(ERDPTH));

  __asm
  
    ENC28J60_RESTORE_APPDATA

    ;; Restore alternate register bank
  
    ld    hl, #VRAM_REGSTATE_FP
    ld    sp, hl
    pop   af
    ex    af, af'     ;; ' apostrophe for syntax coloring
    
    ld    a,  (VRAM_REGSTATE_BP)
    ld    b, a
    ld    a,  (VRAM_REGSTATE_CP)
    ld    c, a
    ld    de, (VRAM_REGSTATE_DEP)
    ld    hl, (VRAM_REGSTATE_HLP)
    exx

    ;; Restore index registers, DE, I, R

    ld    ix, (VRAM_REGSTATE_IX)
    ld    iy, (VRAM_REGSTATE_IY)

    ld    de, (VRAM_REGSTATE_DE)
           
    ld    a, (VRAM_REGSTATE_I)
    ld    i, a
           
    ld    a, (VRAM_REGSTATE_R)
    ld    r, a

    ;; Restore F
  
    ld    hl, #VRAM_REGSTATE_F
    ld    sp, hl
    pop   af
  
    ;; Restore SP & HL
  
    ld    sp, (VRAM_REGSTATE_SP)
    ld    hl, (VRAM_REGSTATE_HL)
                  
    ;; 
    ;; Set up final register state for trampoline
    ;;
          
    ld    bc, #0x9F     ;; SPI control register
    ld    a, #0x20      ;; page out FRAM, pull reset on ENC28J60 low
  
    /*
     * Enable the snippet below to hard-code SP and PC values to the
     * values used by the test image (useful when debugging)
     */
  
#if 0
    ld    hl, #0x7400
    ld    sp, hl
    ld    a, #0x00
    ld    (VRAM_TRAMPOLINE_JP_FINAL + 1), a
    ld    a, #0x70
    ld    (VRAM_TRAMPOLINE_JP_FINAL + 2), a

    ld    a, #0x20
#endif

    jp    VRAM_TRAMPOLINE_START
  
    __endasm;
}
