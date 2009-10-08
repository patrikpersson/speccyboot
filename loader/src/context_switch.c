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
#include "enc28j60_spi.h"

/* ------------------------------------------------------------------------ */

/*
 * Constant table for all 8-bit constants, defined in crt0.asm
 *
 * (used for loading F register using 'pop af')
 */
#define HIBYTE_BYTE_CONSTANTS         (0x3e)

/* ------------------------------------------------------------------------ */

/*
 * VRAM trampoline layout
 *
 *    out (c), a    (emulator test only)
 *    ld  bc, NN    (emulator test only)
 *    ld  a, N
 *    ei / di       (depending on whether interrupts are to be enabled)
 *    jp  NN
 *
 * The trampoline is located at 0x4000. Entry point is 0x4000 for emulator test
 * builds, and 0x3FFE for target builds.
 */

#ifdef EMULATOR_TEST

#define VRAM_TRAMPOLINE_START    0x4000

#define VRAM_TRAMPOLINE_OUT      0x4000
#define VRAM_TRAMPOLINE_LD_BC    0x4002
#define VRAM_TRAMPOLINE_LD_A     0x4005
#define VRAM_TRAMPOLINE_EIDI     0x4007
#define VRAM_TRAMPOLINE_JP       0x4008

#else

#define VRAM_TRAMPOLINE_START    0x3ffe

#define VRAM_TRAMPOLINE_LD_A     0x4000
#define VRAM_TRAMPOLINE_EIDI     0x4002
#define VRAM_TRAMPOLINE_JP       0x4003

#endif

/*
 * A suitable place to temporarily store a 16-bit value
 */
#define VRAM_TRAMPOLINE_WORD_STORAGE    VRAM_TRAMPOLINE_EIDI

/* ------------------------------------------------------------------------ */

void
evacuate_z80_header(const uint8_t *header_data, uint8_t paging_cfg)
{
  enc28j60_write_memory_at(EVACUATED_HEADER,
                           header_data,
                           Z80_HEADER_SIZE);
  
  enc28j60_write_memory_at(SAVED_PAGING_CFG, &paging_cfg, sizeof(uint8_t));
}

/* ------------------------------------------------------------------------ */

void
evacuate_data(void)
{
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
  check_stack();
    
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
  
    ;; First write the ROM selection bit to 0x1FFD bit 2 for +3/+2A:
    ;; this should give us either ROM0 (128k editor) or ROM3 (48k ROM).
    ;; On a regular 128k Spectrum, this write will end up in the 0x7FFD
    ;; register due to incomplete address decoding. However, the correct value
    ;; will be stored immediately after below.
    rra
    rra
    and   #0x04         ;; stay away from the disk motor, use normal paging
    ld    bc, #0x1FFD   ;; 128k +2A/+3 page register
#if 0
    TODO ->> enable this when 128k verified to work
    out   (c), a
#endif

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
    ld    (VRAM_TRAMPOLINE_JP + 1), a
    ld    a, c
    ld    (VRAM_TRAMPOLINE_JP + 2), a

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
    ;; Set up border
    ;;
  
    ld    hl, #flags_done
    ld    c, #Z80_OFFSET_FLAGS
    jp    _enc28j60_load_byte_at_address
flags_done::
    ld    a, c
    rra
    and   #7
    out   (254), a
            
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

    ;;
    ;; Load IFF1 into address VRAM_TRAMPOLINE_LD_A (temporarily)
    ;;
          
    ld    hl, #iff1_done
    ld    c, #Z80_OFFSET_IFF1
    jp    _enc28j60_load_byte_at_address
iff1_done::
    ld    a, c
    ld    (VRAM_TRAMPOLINE_LD_A), a
  
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
    ;; Select different final part depending on whether interrupts
    ;; are to be enabled or disabled
    ;;
    
    ld    a, (VRAM_TRAMPOLINE_LD_A)
    or    a
    jr    z, final_switch_without_interrupts
      
    ;; ------------------------------------------------------------------------
    ;; Final step for enabled interrupts
    ;; ------------------------------------------------------------------------
          
    ;;
    ;; Restore F and SP
    ;;
          
    pop   af
    ld    sp, (VRAM_TRAMPOLINE_WORD_STORAGE)

    ;; 
    ;; Store constant bytes of trampoline (instruction opcodes)
    ;;
              
    ld    a, #0x3E
    ld    (VRAM_TRAMPOLINE_LD_A), a
    ld    a, #0xC3
    ld    (VRAM_TRAMPOLINE_JP), a
    ld    a, #0xFB                    ;; EI
    ld    (VRAM_TRAMPOLINE_EIDI), a
                  
#ifdef EMULATOR_TEST
    ld    a, #0xED
    ld    (VRAM_TRAMPOLINE_OUT), a
    ld    a, #0x79
    ld    (VRAM_TRAMPOLINE_OUT + 1), a
    ld    a, #0x01
    ld    (VRAM_TRAMPOLINE_LD_BC), a
      
    ;; 
    ;; Set up final register state for trampoline
    ;;
          
    ld    bc, #0x7FFD   ;; page register, used by trampoline
    ld    a, #0x30 + DEFAULT_BANK
#else
    ld    a, #0x20      ;; page out FRAM, pull reset on ENC28J60 low
#endif

    jp    VRAM_TRAMPOLINE_START
                        
    ;; ------------------------------------------------------------------------
    ;; Final step for disabled interrupts
    ;; ------------------------------------------------------------------------
final_switch_without_interrupts::
      
    ;;
    ;; Restore F and SP
    ;;
  
    pop   af
    ld    sp, (VRAM_TRAMPOLINE_WORD_STORAGE)

    ;; 
    ;; Store constant bytes of trampoline (instruction opcodes)
    ;;
    
    ld    a, #0x3E
    ld    (VRAM_TRAMPOLINE_LD_A), a
    ld    a, #0xC3
    ld    (VRAM_TRAMPOLINE_JP), a
    ld    a, #0                       ;; NOP, less screen garbage than DI
    ld    (VRAM_TRAMPOLINE_EIDI), a

#ifdef EMULATOR_TEST
    ld    a, #0xED
    ld    (VRAM_TRAMPOLINE_OUT), a
    ld    a, #0x79
    ld    (VRAM_TRAMPOLINE_OUT + 1), a
    ld    a, #0x01
    ld    (VRAM_TRAMPOLINE_LD_BC), a
        
    ;; 
    ;; Set up final register state for trampoline
    ;;
          
    ld    bc, #0x7FFD   ;; page register, used by trampoline
    ld    a, #0x30 + DEFAULT_BANK
#else
    ld    a, #0x20      ;; page out FRAM, pull reset on ENC28J60 low
#endif
  
    /*
     * Enable the snippet below to hard-code SP and PC values to the
     * values used by the test image (useful when debuggning SPI reads)
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
