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

#ifndef SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD
#define SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

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
 * Memory layout for evacuation:
 *
 * address range      size  contents
 * -------------      ----  --------
 * 0x0000 .. 0x3FFF   16kB  SpeccyBoot ROM
 * 0x4000 .. 0x57FF   6kB   Video RAM (bitmap)
 * 0x5800 .. 0x5AFF   768B  Video RAM (attributes, progress display)     (!)
 * 0x5B00 .. 0x5B5F   96B   CPU stack                                    (!)
 * 0x5B60 .. 0x5F30   977B  static variables                             (!)
 * 0x5F31 .. 0x5FFF   207B  font data ' '..'9'                           (!)
 * 0x6000 .. 0x6230   561B  remaining font data
 *
 * The area 0x5800 - 0x5FFF, marked with (!) above, needs to be preserved
 * during snapshot loading. When bytes destined for these addresses are
 * received, they are instead stored in the ENC28J60's on-chip SRAM:
 *
 * 0x1800 .. 0x1FFF   2kB   data destined for addresses 0x5800 .. 0x5FFF in
 *                          the Spectrum RAM (temporary storage during loading)
 * ------------------------------------------------------------------------- */

/*
 * Runtime data (the stuff to evacuate). Note that the z80_loader code
 * requires RUNTIME_DATA_LENGTH to be a multiple of 0x400 (for kilobyte
 * counter display)
 */
#define RUNTIME_DATA                  (0x5800)
#define RUNTIME_DATA_LENGTH           (0x0800)

/*
 * Buffer to write evacuated data into, before we write all off it to the
 * ENC28J60
 */
#define EVACUATION_TEMP_BUFFER        (0x6000)

/*
 * When curr_write_pos passes beyond this address, we know evacuation is done
 */
#define EVACUATION_DONE_ADDR          ((RUNTIME_DATA) + (RUNTIME_DATA_LENGTH))

/* ------------------------------------------------------------------------- */

/*
 * Masks for meaning of snapshot_flags
 */
#define SNAPSHOT_FLAGS_COMPRESSED_MASK  (0x20)

#define HW_TYPE_SPECTRUM_128K           (3)

/* ------------------------------------------------------------------------ */

/*
 * .z80 snapshot file header
 *
 * https://worldofspectrum.org/faq/reference/z80format.htm
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
  uint8_t   int_mode;       /* only bits 0-1, other are bits ignored */

  /*
   * Extended header (versions 2 and later)
   */
  uint16_t                     extended_length;
  uint16_t                     extended_pc;
  uint8_t                      hw_type;
  uint8_t                      hw_state_7ffd;
  uint8_t                      dummy_if1_timex;   /* not used */
  uint8_t                      dummy_hw_mod;      /* not used */
  uint8_t                      hw_state_fffd;
  uint8_t                      hw_state_snd[16];

  /*
   * Remaining contents of this header are useless for a real Spectrum
   */
};

/*
 * Offsets for use by assembly code  (should match header above)
 */
#define Z80_HEADER_OFFSET_A                0
#define Z80_HEADER_OFFSET_F                1
#define Z80_HEADER_OFFSET_BC_HL            2
#define Z80_HEADER_OFFSET_PC               6
#define Z80_HEADER_OFFSET_SP               8
#define Z80_HEADER_OFFSET_I                10
#define Z80_HEADER_OFFSET_R                11
#define Z80_HEADER_OFFSET_MISC_FLAGS       12
#define Z80_HEADER_OFFSET_DE               13
#define Z80_HEADER_OFFSET_BC_P             15
#define Z80_HEADER_OFFSET_DE_P             17
#define Z80_HEADER_OFFSET_HL_P             19
#define Z80_HEADER_OFFSET_A_P              21
#define Z80_HEADER_OFFSET_F_P              22
#define Z80_HEADER_OFFSET_IY               23
#define Z80_HEADER_OFFSET_IX               25
#define Z80_HEADER_OFFSET_IFF1             27
#define Z80_HEADER_OFFSET_INT_MODE         29

#define Z80_HEADER_OFFSET_EXT_PC           32
#define Z80_HEADER_OFFSET_HW_TYPE          34
#define Z80_HEADER_OFFSET_HW_STATE_7FFD    35
#define Z80_HEADER_OFFSET_HW_STATE_FFFD    38

#define Z80_HEADER_OFFSET_HW_STATE_SND     39

/* ------------------------------------------------------------------------ */

/*
 * True if 'p' points to an extended snapshot header, false otherwise
 */
#define IS_EXTENDED_SNAPSHOT_HEADER(p)                                        \
  (((const struct z80_snapshot_header_t *)(p))->pc == 0)

/* ------------------------------------------------------------------------ */

/*
 * True if HW ID 'id' refers to a 128k machine
 *
 * (rather simplistic check, but it will only fail for rather esoteric
 * hardware configurations that I have no way of testing anyway)
 */
#define IS_128K_MACHINE(p)            ((p) >= HW_TYPE_SPECTRUM_128K)

/* ------------------------------------------------------------------------ */

/*
 * Copy a .z80 header from the RX buffer to a separate memory area. The copied
 * header is used by evacuate_data() below at a later time.
 */
#define evacuate_z80_header()       memcpy(&snapshot_header,                  \
                                           &rx_frame.udp.app.tftp.data.z80,   \
                                           sizeof(snapshot_header))

/*
 * Evacuate data from the temporary buffer to ENC28J60 SRAM. Examine the stored
 * .z80 header, and prepare the context switch to use information
 * (register state etc.) in it.
 */
void evacuate_data(void);

/*
 * Restore application data from ENC28J60 SRAM, restore register values
 * and system state from the stored .z80 header, and execute the application.
 */
void context_switch(void);

#endif /* SPECCYBOOT_CONTEXT_SWITCH_INCLUSION_GUARD */
