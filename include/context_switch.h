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

#ifndef SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD
#define SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"

/* -------------------------------------------------------------------------
* Evacuation concerns protecting the RAM used by SpeccyBoot (runtime data)
* while a snapshot is being loaded into RAM. It is performed in three steps:
*
* 1. When loading the snapshot's RAM contents for runtime data area, the
*    loaded data is instead stored in a temporary evacuation buffer (within
*    Spectrum RAM).
*
* 2. When the last byte of the loaded data above has been loaded, the entire
*    temporary evacuation buffer is written to ENC28J60 on-chip SRAM.
*
* 3. When the entire .z80 snapshot has been loaded, the runtime data is
*    overwritten with the corresponding data from ENC28J60 on-chip SRAM.
*
* NOTE: when the EMULATOR_TEST build flag is set, 128k RAM bank 0 is used
* instead of ENC28J60 SRAM.
*
* Memory layout for evacuation:
*
* address range      size  contents
* -------------      ----  --------
* 0x0000 .. 0x3FFF   16kB  SpeccyBoot ROM
* 0x4000 .. 0x57FF   6kB   Video RAM (bitmap)
* 0x5800 .. 0x5AFF   768B  Video RAM (attributes)            \
* 0x5B00 .. 0x5BFF   256B  CPU stack (initial value 0x5BFF)   |   runtime data
* 0x5C00 .. 0x5FFF   1K    static variables                  /
*
* 0x6000 .. 0x67FF   2K    temporary evacuation buffer
* 
* The area 0x5800 - 0x5FFF needs to be preserved during snapshot loading.
* When data destined for these addresses are received, they are instead
* stored in the ENC28J60's on-chip SRAM:
*
* 0x1700 .. 0x17FF   256B  .z80 snapshot header + other config data
* 0x1800 .. 0x1FFF   2K    data destined for addresses 0x5800 .. 0x5FFF in
*                          the Spectrum RAM (temporary storage during loading)
* ------------------------------------------------------------------------- */

/*
 * Runtime data (the stuff to evacuate)
 */
#define RUNTIME_DATA                  (0x5800)
#define RUNTIME_DATA_LENGTH           (0x0800)

/*
 * Buffer to write evacuated data into, before we write all off it to the
 * ENC28J60
 */
#define EVACUATION_TEMP_BUFFER        (0x6000)

/*
 * Size of a .z80 header (the parts of it we care about, at least)
 */
#define Z80_HEADER_SIZE                                                       \
  sizeof(struct z80_snapshot_header_extended_t)

/*
 * When curr_write_pos passes beyond this address, we know evacuation is done
 */
#define EVACUATION_DONE_ADDR          ((RUNTIME_DATA) + (RUNTIME_DATA_LENGTH))

/*
 * Addresses to evacuated data in the ENC28J60 SRAM
 */
#define EVACUATED_HEADER              0x1700
#define EVACUATED_DATA                0x1800
/* ------------------------------------------------------------------------ */

/*
 * .z80 snapshot file header
 *
 * http://www.worldofspectrum.org/faq/reference/z80format.htm
 */
PACKED_STRUCT(z80_snapshot_header_t) {
  uint8_t   a;
  uint8_t   f;
  uint16_t  bc;
  uint16_t  hl;
  uint16_t  pc;
  uint16_t  sp;
  uint8_t   i;
  uint8_t   r;
  uint8_t   snapshot_flags;
  uint16_t  de;
  uint16_t  bc_p;
  uint16_t  de_p;
  uint16_t  hl_p;
  uint8_t   a_p;
  uint8_t   f_p;
  uint16_t  iy;
  uint16_t  ix;
  uint8_t   iff1;
  uint8_t   iff2;
  uint8_t   int_mode;     /* only bits 0-1, other are bits ignored */
};

/*
 * Extended header (versions 2 and later)
 */
PACKED_STRUCT(z80_snapshot_header_extended_t) {
  struct z80_snapshot_header_t default_header;
  uint16_t                     extended_header_bytes;
  uint16_t                     pc;
  uint8_t                      hw_type;
  
  /*
   * Remaining contents of this header are useless for a real Spectrum
   */
};

#define Z80_OFFSET_A           0
#define Z80_OFFSET_F           1

#define Z80_OFFSET_C           2
#define Z80_OFFSET_B           3

#define Z80_OFFSET_L           4
#define Z80_OFFSET_H           5

#define Z80_OFFSET_PC_LO       6
#define Z80_OFFSET_PC_HI       7

#define Z80_OFFSET_SP_LO       8
#define Z80_OFFSET_SP_HI       9

#define Z80_OFFSET_I           10
#define Z80_OFFSET_R           11

#define Z80_OFFSET_FLAGS       12

#define Z80_OFFSET_DE          13

#define Z80_OFFSET_E           13
#define Z80_OFFSET_D           14

#define Z80_OFFSET_CP          15
#define Z80_OFFSET_BP          16

#define Z80_OFFSET_EP          17
#define Z80_OFFSET_DP          18

#define Z80_OFFSET_LP          19
#define Z80_OFFSET_HP          20

#define Z80_OFFSET_AP          21
#define Z80_OFFSET_FP          22

#define Z80_OFFSET_IY_LO       23
#define Z80_OFFSET_IY_HI       24

#define Z80_OFFSET_IX_LO       25
#define Z80_OFFSET_IX_HI       26

#define Z80_OFFSET_IFF1        27

#define Z80_OFFSET_IM          29

#define Z80_OFFSET_PC_V2_LO    32
#define Z80_OFFSET_PC_V2_HI    33

/* ------------------------------------------------------------------------ */

/*
 * Evacuate a .z80 header to ENC28J60 SRAM.
 * Copies up to sizeof(struct z80_snapshot_header_extended_t) bytes.
 */
void evacuate_z80_header(const uint8_t *header_data);

/*
 * Evacuate data from the temporary buffer to ENC28J60 SRAM.
 */
void evacuate_data(void);

/*
 * Restore application data from ENC28J60 SRAM, restore register values
 * and system state from the stored .z80 header, and execute the application.
 */
void context_switch(void);

#endif /* SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD */
