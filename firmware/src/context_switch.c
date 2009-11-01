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

#include <string.h>

#include "context_switch.h"
#include "enc28j60.h"

/* ------------------------------------------------------------------------ */

/*
 * Constant table for all 8-bit constants, defined in crt0.asm
 *
 * (used for loading F register using 'pop af')
 */
#define HIBYTE_BYTE_CONSTANTS         (0x3e)

/* ------------------------------------------------------------------------ */

/*
 * VRAM trampoline layout. Split onto two pixel lines, to reduce the number
 * of distorted character cells.
 *
 * 0x4000:
 *    out (c), a    (emulator test only)
 *    ld  bc, #NN   (emulator test only)
 *    ld  a, #N
 *    jp  0x4100
 *
 * 0x4100:
 *    ei / di       (depending on whether interrupts are to be enabled)
 *    jp  NN
 *
 * The trampoline is located at 0x4000. Entry point is 0x4000 for emulator test
 * builds, and 0x3FFE for target builds.
 */

#ifdef EMULATOR_TEST

#define VRAM_TRAMPOLINE_START           0x4000

#define VRAM_TRAMPOLINE_OUT             0x4000
#define VRAM_TRAMPOLINE_LD_BC           0x4002
#define VRAM_TRAMPOLINE_LD_A            0x4005
#define VRAM_TRAMPOLINE_JP_CONT         0x4007

#else

#define VRAM_TRAMPOLINE_START           0x3ffe

#define VRAM_TRAMPOLINE_LD_A            0x4000
#define VRAM_TRAMPOLINE_JP_CONT         0x4002

#endif

#define VRAM_TRAMPOLINE_EIDI            0x4100
#define VRAM_TRAMPOLINE_JP_FINAL        0x4101

/*
 * A suitable place to temporarily store a 16-bit value
 */
#define VRAM_TRAMPOLINE_WORD_STORAGE    0x4200

/* ------------------------------------------------------------------------ */

/*
 * Sound register index indicating no 128k sound
 */
#define NO_AY_SOUND                     (0xff)

/*
 * Keep AY8912 sound register values in Spectrum RAM
 */
static uint8_t sound_regs[16];
static uint8_t curr_sound_reg = NO_AY_SOUND;   /* Means sound_regs not used */

/* ------------------------------------------------------------------------ */

void
evacuate_z80_header(const uint8_t *header_data,
                    uint8_t paging_cfg,
                    bool valid_sound_regs)
{
  if (valid_sound_regs)  {
    curr_sound_reg = header_data[Z80_OFFSET_CURR_SNDREG];
    memcpy(&sound_regs, &header_data[Z80_OFFSET_SNDREGS], sizeof(sound_regs));
  }
  
  enc28j60_write_memory_at(EVACUATED_HEADER,
                           header_data,
                           Z80_HEADER_SIZE);
  
  enc28j60_write_memory_at(SAVED_PAGING_CFG, &paging_cfg, sizeof(uint8_t));
}

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
  /*const*/ uint8_t *bitmap_ptr = (uint8_t *) BITMAP_BASE;
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
       * Few pixels set: make them background colour
       */
      uint8_t paper_colour = (attr_value & 0x38) >> 3;
      *attr_ptr++ = (attr_value & 0xf8) | paper_colour;
    }
    else {
      /*
       * Many pixels set: make background colour same as foreground
       */
      uint8_t ink_colour = (attr_value & 0x07);
      *attr_ptr++ = (attr_value & 0xc7) | (ink_colour << 3);
    }
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
 * Restore system state. Use a short trampoline, located in video RAM, for
 * the final step.
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
   * Write sound register contents, if valid values exist
   */
  if (curr_sound_reg != NO_AY_SOUND) {
    static sfr banked at(0xfffd) register_select;
    static sfr banked at(0xbffd) register_value;
    
    uint8_t reg;
    
    for (reg = 0; reg < 16; reg++) {
      register_select = reg;
      register_value  = sound_regs[reg];
    }
    register_select = curr_sound_reg;
  }
  
#ifndef EMULATOR_TEST
  /*
   * Select the correct register bank for RDPT operations
   */
  enc28j60_select_bank(BANK(ERDPTH));
#endif
  
  __asm

    di
  
#ifdef EMULATOR_TEST 

    ld    hl, #c_done
    ld    c, #Z80_OFFSET_C
    jp    _enc28j60_load_byte_at_address
c_done::
    ld    a, c
    ld    (VRAM_TRAMPOLINE_LD_BC + 1), a
        
    ld    hl, #b_done
    ld    c, #Z80_OFFSET_B
    jp    _enc28j60_load_byte_at_address
b_done::
    ld    a, c
    ld    (VRAM_TRAMPOLINE_LD_BC + 2), a
#else
    ld    hl, #hw_7ffd_done
    ld    c, #OFFSET_PAGING_CONFIG
    jp    _enc28j60_load_byte_at_address
hw_7ffd_done::
    ld    d, c
    ld    a, c
  
    ;; First write the ROM selection bit to 0x1FFD bit 2 for +3/+2A/+2B:
    ;; this should give us either ROM0 (128k editor) or ROM3 (48k ROM).
    ;; On a regular 128k Spectrum, this write will end up in the 0x7FFD
    ;; register due to incomplete address decoding -- see the "Memory" section
    ;; in http://www.worldofspectrum.org/faq/reference/128kreference.htm.
    ;; However, the correct value will be stored immediately after below.
    ;;
    ;; NOTE: this has NOT been tested on the +3/+2A/+2B
                                                    
    rra
    rra
    and   #0x04         ;; stay away from the disk motor, use normal paging
    ld    bc, #0x1FFD   ;; 128k +2A/+2B/+3 page register
    out   (c), a
    ld    b, #0x7F      ;; 128k page register
    out   (c), d
#endif

    ENC28J60_RESTORE_APPDATA

    ld    hl, #a_done
    ld    c, #Z80_OFFSET_A
    jp    _enc28j60_load_byte_at_address
a_done::
    ld    a, c
    ld    (VRAM_TRAMPOLINE_LD_A + 1), a
  
    ;; 
    ;; Store constant bytes of trampoline (instruction opcodes)
    ;;
    
    ld    a, #0x3E
    ld    (VRAM_TRAMPOLINE_LD_A), a
    ld    a, #0xC3
    ld    (VRAM_TRAMPOLINE_JP_FINAL), a
  
    ld    hl, #VRAM_TRAMPOLINE_JP_CONT
    ld    (hl), a
    inc   hl
    ld    (hl), #0          ;; LOBYTE(0x4100)
    inc   hl
    ld    (hl), #0x41       ;; HIBYTE(0x4100)
  
#ifdef EMULATOR_TEST
    ld    a, #0xED
    ld    (VRAM_TRAMPOLINE_OUT), a
    ld    a, #0x79
    ld    (VRAM_TRAMPOLINE_OUT + 1), a
    ld    a, #0x01
    ld    (VRAM_TRAMPOLINE_LD_BC), a
#endif
  
    ;;
    ;; Check IFF1, load EI or DI into address VRAM_TRAMPOLINE_EIDI
    ;;
    
    ld    hl, #iff1_done
    ld    c, #Z80_OFFSET_IFF1
    jp    _enc28j60_load_byte_at_address
    iff1_done::
    ld    a, c
    or    a
    ld    a, #0xF3        ;; DI
    jr    z, store_eidi
    ld    a, #0xFB        ;; EI
store_eidi::
    ld    (VRAM_TRAMPOLINE_EIDI), a
  
    ;;
    ;; pick correct PC value, depending on snapshot format version
    ;;
    
    ld    hl, #pc_lo_done1
    ld    c, #Z80_OFFSET_PC_LO
    jp    _enc28j60_load_byte_at_address
pc_lo_done1::
    ld    e, c
      
    ld    hl, #pc_hi_done1
    ld    c, #Z80_OFFSET_PC_HI
    jp    _enc28j60_load_byte_at_address
pc_hi_done1::
    ld    a, c
    or    e
      
    jr    nz, header_version_1

    ld    hl, #pc_lo_done2
    ld    c, #Z80_OFFSET_PC_V2_LO
    jp    _enc28j60_load_byte_at_address
pc_lo_done2::
    ld    e, c
    
    ld    hl, #pc_hi_done2
    ld    c, #Z80_OFFSET_PC_V2_HI
    jp    _enc28j60_load_byte_at_address
pc_hi_done2::
      
header_version_1::
    ld    a, e
    ld    (VRAM_TRAMPOLINE_JP_FINAL + 1), a
    ld    a, c
    ld    (VRAM_TRAMPOLINE_JP_FINAL + 2), a

    ;;
    ;; Set up registers IX & IY using VRAM_TRAMPOLINE_WORD_STORAGE for
    ;; temporary storage
    ;;
  
    ld    hl, #ix_lo_done
    ld    c, #Z80_OFFSET_IX_LO
    jp    _enc28j60_load_byte_at_address
ix_lo_done::
    ld    e, c
      
    ld    hl, #ix_hi_done
    ld    c, #Z80_OFFSET_IX_HI
    jp    _enc28j60_load_byte_at_address
ix_hi_done::
    ld    d, c
    
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), de
    ld    ix, (VRAM_TRAMPOLINE_WORD_STORAGE)
  
    ld    hl, #iy_lo_done
    ld    c, #Z80_OFFSET_IY_LO
    jp    _enc28j60_load_byte_at_address
iy_lo_done::
    ld    e, c
    
    ld    hl, #iy_hi_done
    ld    c, #Z80_OFFSET_IY_HI
    jp    _enc28j60_load_byte_at_address
iy_hi_done::
    ld    d, c
    
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), de
    ld    iy, (VRAM_TRAMPOLINE_WORD_STORAGE)
  
    ;;
    ;; Set up registers AF', BC', DE', HL'
    ;;
    
    ld    hl, #ap_done
    ld    c, #Z80_OFFSET_AP
    jp    _enc28j60_load_byte_at_address
ap_done::
    ld    e, c
      
    ld    hl, #fp_done
    ld    c, #Z80_OFFSET_FP
    jp    _enc28j60_load_byte_at_address
fp_done::
    ld    h, #HIBYTE_BYTE_CONSTANTS
    ld    l, c
    ld    sp, hl
    pop   af
    ld    a, e
    ex    af, af'     ;; this apostrophe ' is just to fix syntax coloring...
      
    ld    hl, #dp_done
    ld    c, #Z80_OFFSET_DP
    jp    _enc28j60_load_byte_at_address
dp_done::
    ld    d, c
      
    ld    hl, #ep_done
    ld    c, #Z80_OFFSET_EP
    jp    _enc28j60_load_byte_at_address
ep_done::
    ld    e, c
      
    ld    hl, #hp_done
    ld    c, #Z80_OFFSET_HP
    jp    _enc28j60_load_byte_at_address
hp_done::
    ld    a, c
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE + 1), a
      
    ld    hl, #lp_done
    ld    c, #Z80_OFFSET_LP
    jp    _enc28j60_load_byte_at_address
lp_done::      
    ld    a, c
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), a
  
    ld    hl, #bp_done
    ld    c, #Z80_OFFSET_BP
    jp    _enc28j60_load_byte_at_address
bp_done::
    ld    b, c
    
    ld    hl, #cp_done
    ld    c, #Z80_OFFSET_CP
    jp    _enc28j60_load_byte_at_address
cp_done::

    ld    hl, (VRAM_TRAMPOLINE_WORD_STORAGE)

    exx
  
    ;;
    ;; Load final SP value into temporary storage (for later)
    ;;
    
    ld    hl, #sp_lo_done
    ld    c, #Z80_OFFSET_SP_LO
    jp    _enc28j60_load_byte_at_address
sp_lo_done::
    ld    e, c
    
    ld    hl, #sp_hi_done
    ld    c, #Z80_OFFSET_SP_HI
    jp    _enc28j60_load_byte_at_address
sp_hi_done::
    ld    d, c
    
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), de

    ;;
    ;; Set up interrupt mode and I register
    ;;
    
    ld    hl, #im_done
    ld    c, #Z80_OFFSET_IM
    jp    _enc28j60_load_byte_at_address
im_done::
    ld    a, c
    and   #3     ;; ignore bits 2-7
    jr    z, set_im0
    cp    #1
    jr    z, set_im1
    im    2
    jr    im_set
set_im0::
    im    0
    jr    im_set
set_im1::
    im    1
im_set::

    ld    hl, #i_done
    ld    c, #Z80_OFFSET_I
    jp    _enc28j60_load_byte_at_address
i_done::
    ld    a, c
    ld    i, a
  
    ;;
    ;; Set up border, pick out bit 7 of R
    ;;
    
    ld    hl, #flags_done
    ld    c, #Z80_OFFSET_FLAGS
    jp    _enc28j60_load_byte_at_address
    flags_done::
    ld    a, c
    ld    e, #0
    rra
    rr    e         ;; E.bit7 now holds R.bit7
    and   #7
    out   (254), a
    
    ;;
    ;; Restore R
    ;;
    ;; Adjusted by a carefully calibrated value to match remaining trampoline,
    ;; resulting in the proper value of R after completed context switch.
    ;;
    ;; REG_R_OFFSET is computed as follows:
    ;;
    ;; - Set the value REG_R_OFFSET to 0 (initially)
    ;; - Run one of the test applications (checker?.z80 on target,
    ;;   testimg?.z80 for EMULATOR_TEST builds)
    ;; - N = R value displayed (in binary) in test application
    ;; - E = expected value of R, according to tests/register-values.h (0x2E)
    ;; - Set final REG_R_OFFSET := (E - N)
    ;;
    ;; Separate values for EMULATOR_TEST and regular builds are required.
    ;;
  
#ifdef EMULATOR_TEST
#define REG_R_OFFSET      (0x11)
#else
#define REG_R_OFFSET      (0x1c)
#endif
  
    ld    hl, #r_done
    ld    c, #Z80_OFFSET_R
    jp    _enc28j60_load_byte_at_address
r_done::
    ld    a, c
    add   a, #REG_R_OFFSET
    and   #0x7f
    or    e
    ld    r, a
  
    ;;
    ;; Prepare temporary SP for restoring F
    ;;

    ld    hl, #f_done
    ld    c, #Z80_OFFSET_F
    jp    _enc28j60_load_byte_at_address
f_done::
    ld    h, #HIBYTE_BYTE_CONSTANTS
    ld    l, c

    ld    sp, hl

    ;;
    ;; Restore DE
    ;;
    
    ld    hl, #d_done
    ld    c, #Z80_OFFSET_D
    jp    _enc28j60_load_byte_at_address
d_done::
    ld    d, c
      
    ld    hl, #e_done
    ld    c, #Z80_OFFSET_E
    jp    _enc28j60_load_byte_at_address
e_done::
    ld    e, c
  
#ifndef EMULATOR_TEST
    ;;
    ;; Restore BC
    ;;
    
    ld    hl, #b_done
    ld    c, #Z80_OFFSET_B
    jp    _enc28j60_load_byte_at_address
b_done::
    ld    b, c
    
    ld    hl, #c_done
    ld    c, #Z80_OFFSET_C
    jp    _enc28j60_load_byte_at_address
c_done::
#endif
    
    ;;
    ;; Restore HL
    ;;
    
    ENC28J60_LOAD_HL
          
    ;;
    ;; Restore F and SP
    ;;
          
    pop   af
    ld    sp, (VRAM_TRAMPOLINE_WORD_STORAGE)
                  
    ;; 
    ;; Set up final register state for trampoline
    ;;
          
#ifdef EMULATOR_TEST
    ld    bc, #0x7FFD   ;; page register, used by trampoline
    ld    a, #0x30 + DEFAULT_BANK
#else
    ld    a, #0x20      ;; page out FRAM, pull reset on ENC28J60 low
#endif
  
    /*
     * Enable the snippet below to hard-code SP and PC values to the
     * values used by the test image (useful when debugging SPI reads)
     */
#if 0
    ld    hl, #0x7400
    ld    sp, hl
    ld    a, #0x00
    ld    (VRAM_TRAMPOLINE_JP + 1), a
    ld    a, #0x70
    ld    (VRAM_TRAMPOLINE_JP + 2), a

    ld    a, #0x20
#endif

    jp    VRAM_TRAMPOLINE_START
  
    __endasm;
}
