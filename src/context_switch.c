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

#include "util.h"

#ifdef EMULATOR_TEST

/* ------------------------------------------------------------------------ */

/*
 * ENC28J60 emulation on Spectrum 128k (for test)
 *
 * The Spectrum 128k pages are used as follows:
 *
 * 0      Temporary storage (emulating ENC28J60 on-chip SRAM)
 * 1      mapped to 0xc000 for 48k application
 * 2      mapped to 0x8000 (static)
 * 3      1st part of embedded .z80 snapshot
 * 4      2nd part of embedded .z80 snapshot
 * 5      mapped to 0x4000 (static)
 * 6      3d part of embedded .z80 snapshot
 * 7      4th part of embedded .z80 snapshot
 */

/* ------------------------------------------------------------------------ */

/*
 * Storage for header data
 */
static uint8_t __at(0xc000 + EVACUATED_HEADER)
  saved_header_data[Z80_HEADER_SIZE];

/*
 * Storage for evacuated application data
 */
static uint8_t __at(0xc000 + EVACUATED_DATA)
  saved_app_data[RUNTIME_DATA_LENGTH];

/* ------------------------------------------------------------------------ */

/*
 * Emulation of the corresponding routine in enc28j60_spi.c
 *
 * IY holds return address
 * L holds offset of byte to read
 *
 * AF destroyed
 *
 * The byte is returned in L
 */
static void
enc28j60_load_byte_at_address(void)
__naked
{
  __asm

  ;; store BC and H somewhere good (will distort picture in top right)
  
  ld    (0x401d), bc
  ld    a, h
  ld    (0x401f), a
  
  ;; switch to bank 0, read byte, switch back to bank 1
    
  xor   a
  ld    bc, #0x7ffd
  out   (c), a
  
  ld    h, #0xc0
  ld    bc, #EVACUATED_HEADER
  add   hl, bc
  ld    l, (hl)

  ld    bc, #0x7ffd
  inc   a
  out   (c), a
    
  ld    bc, (0x401d)
  ld    a, (0x401f)
  ld    h, a
  ld    a, r      ;; ensure we do not somehow depend on value of A
    
  jp    (iy)
  
  __endasm;
}

/* ------------------------------------------------------------------------ */

/*
 * Emulation of the corresponding routine in enc28j60_spi.c
 *
 * Loads IY from the correct location, then jumps to address
 * IY_JUMP_DONE.
 */

#define IY_JUMP_DONE    iy_done

static void
enc28j60_load_iy_and_jump(void)
__naked
{
  __asm
  
    ;; store BC and HL somewhere good (will distort picture in top right)
      
    ld    (0x401c), bc
    ld    (0x401e), hl
    
    ;; switch to bank 0, read IY, switch back to bank 1
      
    xor   a
    ld    bc, #0x7ffd
    out   (c), a
        
    ld    hl, #0xc000 + EVACUATED_HEADER + Z80_OFFSET_IY_LO
    ld    a, (hl)
    .db   #0xFD
    ld    l, a      ;; ld IYl, a
    inc   hl
    ld    a, (hl)
    .db   #0xFD
    ld    h, a      ;; ld IYh, a
        
    ld    bc, #0x7ffd
    ld    a, #1
    out   (c), a
        
    ld    bc, (0x401c)
    ld    hl, (0x401e)
          
    jp    IY_JUMP_DONE
      
  __endasm;
}

/* ------------------------------------------------------------------------ */

#else

#include "enc28j60_spi.h"

#endif

/* ------------------------------------------------------------------------ */

/*
 * Constant table for all 8-bit constants, defined in crt0.asm
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

/*
 * Restore system state. Use a short trampoline, located in video RAM, for
 * the final step.
 */
void context_switch_using_vram(void)
__naked
{
  __asm
    
    di
  
#ifdef EMULATOR_TEST
  
    ld    iy, #c_done
    ld    l, #Z80_OFFSET_C
    jp    _enc28j60_load_byte_at_address
c_done::
    ld    a, l
    ld    (VRAM_TRAMPOLINE_LD_BC + 1), a
        
    ld    iy, #b_done
    ld    l, #Z80_OFFSET_B
    jp    _enc28j60_load_byte_at_address
b_done::
    ld    a, l
    ld    (VRAM_TRAMPOLINE_LD_BC + 2), a

    ;;
    ;; Restore application data
    ;;
    
    xor   a
    ld    bc, #0x7ffd
    out   (c), a
    ld    hl, #_saved_app_data
    ld    de, #RUNTIME_DATA
    ld    bc, #RUNTIME_DATA_LENGTH
    ldir
      
#else
  
    ld    bc, #0x7FFD   ;; page register
    ld    a, #0x31      ;; page 1 at 0xc000, 48k ROM, lock paging
    out   (c), a
      
    ;;
    ;; Restore application data
    ;;
    -- FIXME --
      
#endif

    ld    iy, #a_done
    ld    l, #Z80_OFFSET_A
    jp    _enc28j60_load_byte_at_address
a_done::
    ld    a, l
    ld    (VRAM_TRAMPOLINE_LD_A + 1), a

    ;;
    ;; pick correct PC value, depending on snapshot format version
    ;;
    
    ld    iy, #pc_lo_done1
    ld    l, #Z80_OFFSET_PC_LO
    jp    _enc28j60_load_byte_at_address
pc_lo_done1::
    ld    e, l
      
    ld    iy, #pc_hi_done1
    ld    l, #Z80_OFFSET_PC_HI
    jp    _enc28j60_load_byte_at_address
pc_hi_done1::
    ld    a, l
    or    e
      
    jr    nz, header_version_1

    ld    iy, #pc_lo_done2
    ld    l, #Z80_OFFSET_PC_V2_LO
    jp    _enc28j60_load_byte_at_address
pc_lo_done2::
    ld    e, l
    
    ld    iy, #pc_hi_done2
    ld    l, #Z80_OFFSET_PC_V2_HI
    jp    _enc28j60_load_byte_at_address
pc_hi_done2::
      
header_version_1::
    ld    a, e
    ld    (VRAM_TRAMPOLINE_JP + 1), a
    ld    a, l
    ld    (VRAM_TRAMPOLINE_JP + 2), a

    ;;
    ;; Set up register IX VRAM_TRAMPOLINE_WORD_STORAGE for
    ;; temporary storage
    ;;

    ld    iy, #ix_lo_done
    ld    l, #Z80_OFFSET_IX_LO
    jp    _enc28j60_load_byte_at_address
ix_lo_done::
    ld    e, l
      
    ld    iy, #ix_hi_done
    ld    l, #Z80_OFFSET_IX_HI
    jp    _enc28j60_load_byte_at_address
ix_hi_done::
    ld    d, l
    
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), de
    ld    ix, (VRAM_TRAMPOLINE_WORD_STORAGE)

      
    ;;
    ;; Set up register IY using VRAM_TRAMPOLINE_WORD_STORAGE for
    ;; temporary storage
    ;;
      
    ;;
    ;; Load SP into VRAM_TRAMPOLINE_WORD_STORAGE
    ;;
    
    ld    iy, #sp_lo_done
    ld    l, #Z80_OFFSET_SP_LO
    jp    _enc28j60_load_byte_at_address
sp_lo_done::
    ld    e, l
      
    ld    iy, #sp_hi_done
    ld    l, #Z80_OFFSET_SP_HI
    jp    _enc28j60_load_byte_at_address
sp_hi_done::
    ld    d, l
    
    ld    (VRAM_TRAMPOLINE_WORD_STORAGE), de
        
    ;;
    ;; Set up registers AF', BC', DE', HL'
    ;;
    
    ld    iy, #ap_done
    ld    l, #Z80_OFFSET_AP
    jp    _enc28j60_load_byte_at_address
ap_done::
    ld    e, l
      
    ld    iy, #fp_done
    ld    l, #Z80_OFFSET_FP
    jp    _enc28j60_load_byte_at_address
fp_done::
    ld    h, #HIBYTE_BYTE_CONSTANTS
    ld    sp, hl
    pop   af
    ld    a, e
    ex    af, af'     ;; this apostrophe ' is just to fix syntax coloring...
      
    ld    iy, #bp_done
    ld    l, #Z80_OFFSET_BP
    jp    _enc28j60_load_byte_at_address
bp_done::
    ld    b, l
      
    ld    iy, #cp_done
    ld    l, #Z80_OFFSET_CP
    jp    _enc28j60_load_byte_at_address
cp_done::
    ld    c, l
      
    ld    iy, #dp_done
    ld    l, #Z80_OFFSET_DP
    jp    _enc28j60_load_byte_at_address
dp_done::
    ld    d, l
      
    ld    iy, #ep_done
    ld    l, #Z80_OFFSET_EP
    jp    _enc28j60_load_byte_at_address
ep_done::
    ld    e, l
      
    ld    iy, #hp_done
    ld    l, #Z80_OFFSET_HP
    jp    _enc28j60_load_byte_at_address
hp_done::
    ld    h, l
      
    ld    iy, #lp_done
    ld    l, #Z80_OFFSET_LP
    jp    _enc28j60_load_byte_at_address
lp_done::      
    exx
      
#ifndef EMULATOR_TEST

    ;;
    ;; Restore BC
    ;;
    
    ld    iy, #c_done
    ld    l, #Z80_OFFSET_C
    jp    _enc28j60_load_byte_at_address
c_done::
    ld    c, l
      
    ld    iy, #b_done
    ld    l, #Z80_OFFSET_B
    jp    _enc28j60_load_byte_at_address
b_done::
    ld    b, l
#endif
      
    ;;
    ;; Set up interrupt mode and I register
    ;;
    
    ld    iy, #im_done
    ld    l, #Z80_OFFSET_IM
    jp    _enc28j60_load_byte_at_address
im_done::
    ld    a, l
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

    ld    iy, #i_done
    ld    l, #Z80_OFFSET_I
    jp    _enc28j60_load_byte_at_address
i_done::
    ld    a, l
    ld    i, a

#if 0
    ;;
    ;; Set up border
    ;;
    
#define DST   d
#define ADDR  EVACUATED_HEADER + Z80_OFFSET_FLAGS
#include  "read_byte_to_reg.asm-snippet"
#undef DST
#undef ADDR
    
    ld    a, d
    rra
    and   #7
    out   (254), a
#endif
            
    ;;
    ;; Prepare temporary SP for restoring F
    ;;

    ld    iy, #f_done
    ld    l, #Z80_OFFSET_F
    jp    _enc28j60_load_byte_at_address
f_done::

    ld    h, #HIBYTE_BYTE_CONSTANTS
    ld    sp, hl

    ;;
    ;; Restore DE & H (wait with L until below)
    ;;
    
    ld    iy, #d_done
    ld    l, #Z80_OFFSET_D
    jp    _enc28j60_load_byte_at_address
d_done::
    ld    d, l
      
    ld    iy, #e_done
    ld    l, #Z80_OFFSET_E
    jp    _enc28j60_load_byte_at_address
e_done::
    ld    e, l
      
    ld    iy, #h_done
    ld    l, #Z80_OFFSET_H
    jp    _enc28j60_load_byte_at_address
h_done::
    ld    h, l

    ;;
    ;; Load IFF1 into address VRAM_TRAMPOLINE_LD_A (temporarily)
    ;;
          
    ld    iy, #iff1_done
    ld    l, #Z80_OFFSET_IFF1
    jp    _enc28j60_load_byte_at_address
iff1_done::
    ld    a, l
    ld    (VRAM_TRAMPOLINE_LD_A), a
      
    ;;
    ;; Restore L and IY
    ;;
      
    ld    iy, #l_done
    ld    l, #Z80_OFFSET_L
    jp    _enc28j60_load_byte_at_address
l_done::
    
    jp    _enc28j60_load_iy_and_jump
iy_done::
      
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
    ld    a, #0x31      ;; page 1 at 0xc000, 48k ROM, lock paging
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
    ld    a, #0x31      ;; page 1 at 0xc000, 48k ROM, lock paging
#else
    ld    a, #0x20      ;; page out FRAM, pull reset on ENC28J60 low
#endif

    jp    VRAM_TRAMPOLINE_START
                
    __endasm;
}

/* ------------------------------------------------------------------------ */

void evacuate_z80_header(const uint8_t *header_data)
{
  uint8_t i;
  
  select_bank(0);
  
  for (i = 0; i < Z80_HEADER_SIZE; i++) {
    saved_header_data[i] = header_data[i];
  }
  
  select_bank(1);
}

/* ------------------------------------------------------------------------ */

void evacuate_data(void)
{
  uint16_t i;
  
  select_bank(0);
  
  for (i = 0; i < RUNTIME_DATA_LENGTH; i++) {
    saved_app_data[i] = *((uint8_t *) EVACUATION_TEMP_BUFFER + i);
  }
  
  select_bank(1);
}

/* ------------------------------------------------------------------------ */

void context_switch(void)
{
  context_switch_using_vram();
}