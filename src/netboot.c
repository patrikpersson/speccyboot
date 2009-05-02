/*
 * Module netboot:
 *
 * Loads and executes a ZX Spectrum image over TFTP.
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

#include <stddef.h>

#include "netboot.h"

#include "dhcp.h"
#include "tftp.h"

#include "util.h"
#include "logging.h"

/* -------------------------------------------------------------------------
 *
 * The memory map looks as follows:
 *
 * address range      size  contents
 * -------------      ----  --------
 * 0x0000 .. 0x3FFF   16kB  FRAM
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
 * 0x17A9 .. 0x17FF   87B   .z80 snapshot header
 * 0x1800 .. 0x1FFF   2K    data destined for addresses 0x5800 .. 0x5FFF in
 *                          the Spectrum RAM (temporary storage during loading)
 * -------------------------------------------------------------------------
 *
 * The following states are assumed while loading a .z80 snapshot:
 *
 * address range      state     action
 * -------------      -----     ------
 *       --           HEADER    load header to ENC28J60 SRAM
 * 0x4000 .. 0x57FF   SCREEN    load bytes to their final destination
 * 0x5800 .. 0x5FFF   EVACUATE  load bytes into evacuation buffer; after
 *                              loading the final byte at 0x5FFF, write all
 *                              2K into the ENC28J60 SRAM from 0x1800 forward
 * 0x6000 .. 0xFFFF   MAIN      load bytes to their final destination; the
 *                              variable current_bank indicates which bank
 *                              we're in
 * ------------------------------------------------------------------------- */

/*
 * Runtime data (the stuff to evacuate)
 */
#define RUNTIME_DATA                  ((uint8_t *) 0x5800)
#define RUNTIME_DATA_LENGTH           (0x0800)

/*
 * Buffer to write evacuated data into, before we write all off it to the
 * ENC28J60
 */
#define EVACUATION_TEMP_BUFFER        ((uint8_t *) 0x6000)

/*
 * Offset for writing runtime data, to make it all end up in the temp buffer
 * instead
 */
#define EVACUATION_OFFSET             (EVACUATION_TEMP_BUFFER - RUNTIME_DATA)

#define ENC28J60_HEADER_BUFFER        (0x17A9)
#define ENC28J60_EVACUATION_BUFFER    (0x1800)

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
   * Remaining contents of this header is useless for a real Spectrum
   */
};

/*
 * Sub-header for a 16K bank of memory
 */
PACKED_STRUCT(z80_snapshot_bank_header_t) {
  uint16_t    bytes_remaining;          /* 0xffff means uncompressed */
  uint8_t     page;
};

/*
 * Size of a memory bank
 */
#define BANK_LENGTH                     (0x4000)

/*
 * Special value for 'bytes_remaining' indicating no compression used
 */
#define BANK_LENGTH_UNCOMPRESSED        (0xffff)
/* ------------------------------------------------------------------------- */

/*
 * Masks for meaning of snapshot_flags
 */
#define SNAPSHOT_FLAGS_R7_MASK          (0x01)
#define SNAPSHOT_FLAGS_SAMROM_MASK      (0x10)
#define SNAPSHOT_FLAGS_COMPRESSED_MASK  (0x20)

#define SNAPSHOT_FLAGS_BORDER(f)        (((f) >> 1) & 0x07)

/*
 * The only value for hw_type we support
 */
#define HW_TYPE_SPECTRUM_48K            (0)

#define Z80_ESCAPE                      (0xED)

/* ------------------------------------------------------------------------- */

#define Z80_SNAPSHOT_FILENAME     "speccyboot_stage2.z80"

/*
 * TODO
 */
#define enc28j60_write_memory_at(x,y,z)

/* ------------------------------------------------------------------------- */

static enum {
  COMPRESSION_NONE,       /* one single uncompressed 48k block */
  COMPRESSION_V1,         /* one single compressed 48k block */
  COMPRESSION_V2          /* three compressed 16k blocks */
} compression_method = COMPRESSION_NONE;

/* ------------------------------------------------------------------------- */

/*
 * Indicates an evacuation is ongoing (se HANDLE_EVACUATION() below)
 */
static bool evacuating = false;

/*
 * Current position to write to (video RAM)
 */
static uint8_t *curr_write_pos = (uint8_t *) 0x4000;

/* ------------------------------------------------------------------------- */

/*
 * bank_subheader.bytes_remaining   holds the number of bytes remaining in the
 *                                  current bank
 */
static struct z80_snapshot_bank_header_t bank_subheader;

static bool    bank_is_compressed;

/*
 * Starts out as 3 == sizeof(bank_subheader) before each bank is received. This
 * header must be loaded for the remaining memory data to be handled properly.
 */
static uint8_t bank_subheader_bytes_expected = sizeof(bank_subheader);

/* ------------------------------------------------------------------------- */

/*
 * Evacuates the header from the TFTP data block. Returns the length of the
 * header (i.e., the offset of snapshot data within the TFTP data block)
 *
 * This function does some header parsing; it initializes compression_method,
 * sets the border colour, and verifies compatibility.
 */
/*static*/ uint16_t
evacuate_header(const struct z80_snapshot_header_extended_t *header)
{
  uint16_t header_length = sizeof(header->default_header);
  uint8_t          flags = header->default_header.snapshot_flags;
  uint8_t         border = SNAPSHOT_FLAGS_BORDER(flags);
  
  if (header->default_header.pc != 0) {
    /*
     * Version 1 header found
     */
    if (flags & SNAPSHOT_FLAGS_SAMROM_MASK) {
      fatal_error(FATAL_ERROR_INCOMPATIBLE);
    }
    
    compression_method = (flags & SNAPSHOT_FLAGS_COMPRESSED_MASK)
                          ? COMPRESSION_V1 : COMPRESSION_NONE;  
  }
  else {
    /*
     * Version 2 or version 3 header found
     */
    header_length      += header->extended_header_bytes + 2;
    compression_method  = COMPRESSION_V2;
    
    if (header->hw_type != HW_TYPE_SPECTRUM_48K) {
      fatal_error(FATAL_ERROR_INCOMPATIBLE);
    }
  }

  set_border(border);

  enc28j60_write_memory_at(ENC28J60_HEADER_BUFFER,
                           (const uint8_t *) header,
                           header_length);

  return header_length;
}

/* ------------------------------------------------------------------------- */

#define HANDLE_EVACUATION()                                                   \
    if (curr_write_pos == RUNTIME_DATA) {                                     \
      evacuating     = true;                                                  \
      curr_write_pos = EVACUATION_TEMP_BUFFER;                                \
    }                                                                         \
    else if (evacuating                                                       \
             && (curr_write_pos == EVACUATION_TEMP_BUFFER                     \
                                   + RUNTIME_DATA_LENGTH))                    \
    {                                                                         \
      enc28j60_write_memory_at(ENC28J60_EVACUATION_BUFFER,                    \
                               (const uint8_t *) EVACUATION_TEMP_BUFFER,      \
                               RUNTIME_DATA_LENGTH);                          \
      curr_write_pos = RUNTIME_DATA + RUNTIME_DATA_LENGTH;                    \
      evacuating     = false;                                                 \
    }

/* ------------------------------------------------------------------------- */

/*static*/ void
accept_data(const uint8_t *received_data, uint16_t data_length)
{
  static bool escape_byte_received;
  
  switch (compression_method) {
    case COMPRESSION_NONE:
      /*
       * Write bytes without decompression, and without caring about
       * banks.
       */
      while (data_length--) {
        *curr_write_pos++ = *received_data++;
        HANDLE_EVACUATION();
      }
      break;
    case COMPRESSION_V1:
      fatal_error(FATAL_ERROR_INCOMPATIBLE);    /* TODO: implement this */
      break;
    case COMPRESSION_V2:
      while (data_length--) {
        
        /*
         * If currently expecting a new memory bank, ensure the header is
         * loaded.
         */
        if (bank_subheader_bytes_expected && data_length) {
          while (bank_subheader_bytes_expected && data_length) {
            uint8_t *dst_ptr = ((uint8_t *) &bank_subheader)
                               + sizeof(bank_subheader)
                               - bank_subheader_bytes_expected;
            *dst_ptr = *received_data++;
            data_length --;
            bank_subheader_bytes_expected --;
          }
          if (data_length == 0) break;
          
          /*
           * A header has now been loaded. Set up
           */
          if (bank_subheader.bytes_remaining == BANK_LENGTH_UNCOMPRESSED) {
            bank_is_compressed             = false;
            bank_subheader.bytes_remaining = BANK_LENGTH;
          }
          else {
            bank_is_compressed = true;
          }
          
          escape_byte_received = false;
          
          switch (bank_subheader.page) {
            case 4:   curr_write_pos = (uint8_t *) 0x8000;    break;
            case 5:   curr_write_pos = (uint8_t *) 0xC000;    break;
            case 8:   curr_write_pos = (uint8_t *) 0x4000;    break;
            default:
              fatal_error(FATAL_ERROR_INCOMPATIBLE);
          }
        }
        
        if (bank_is_compressed) {
          uint8_t b = *received_data++;
          bank_subheader.bytes_remaining --;
          if (escape_byte_received) {
            
          }
          else if (b == Z80_ESCAPE) {
            escape_byte_received = true;
          }
          else {
            *curr_write_pos++ = b;
          }
        }
        else {
          *curr_write_pos++ = *received_data++;
          bank_subheader.bytes_remaining --;
        }
        if (LOBYTE((uint16_t) curr_write_pos) == 0) {
          HANDLE_EVACUATION();
        }
        
      }
      break;
  }
}

/* ========================================================================= */

void
netboot_do(void)
{
  logging_init();

#if EMULATOR_TEST
  tftp_read_request(NULL);
#else
  eth_init();
  dhcp_init();

  eth_handle_incoming_frames();
#endif
}

/* ------------------------------------------------------------------------- */

/*
 * Called by DHCP (see dhcp.h)
 */
void
netboot_notify_dhcp_state(enum dhcp_state_t state)
{
  uint16_t bar_len;
  switch (state) {
    case STATE_REQUESTING:
      bar_len = 12;
      break;
    case STATE_BOUND:
      bar_len = 24;
      
      tftp_read_request("test.scr");

      break;
    case STATE_SELECTING:
    default:
      bar_len = 0;
      break;
  }
  
  /*
   * Display a progress bar
   */
  set_attrs(PAPER(WHITE) | INK(WHITE) | BRIGHT, 20, 4, bar_len);
  set_attrs(PAPER(GREEN) | INK(GREEN) | BRIGHT, 20, 4 + bar_len, 24 - bar_len);
}

/* ------------------------------------------------------------------------- */

/*
 * Called by TFTP (see tftp.h) when data is received
 */
void
netboot_notify_tftp_data(const uint8_t *received_data,
                         uint16_t       nbr_of_bytes_received,
                         bool           more_data_expected)
{
  /*
   * Indicates that the header has not yet been loaded.
   *
   * NOTE: this code assumes the header to be received in one single block, and
   * not split over multiple blocks. Since the header is always first, and each
   * block is 512 bytes large, this seems like a safe bet to me.
   */
  static bool header_loaded = false;
  
  if (! header_loaded) {
    uint16_t consumed
      = evacuate_header((const struct z80_snapshot_header_extended_t *)
                        received_data);
    
    received_data         += consumed;
    nbr_of_bytes_received -= consumed;
    
    header_loaded = true;
  }

  accept_data(received_data, nbr_of_bytes_received);
  
  if (! more_data_expected) {
    uint16_t i = 0;
    const uint8_t *p = (const uint8_t *) 0xfe00;

    for (i = 0; i < 250; i++) {
      __asm
      halt
      __endasm;
    }

    /*
     * Figure out how much stack we consumed
     */
    i = 0;
    while (*p++ == 0xC0)  i++;

    {
      uint8_t hidigits = i / 100;
      uint8_t lodigits = i % 100;
      
      for (;;) {
        display_digits(hidigits);
        __asm
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        __endasm;
        display_digits(lodigits);
        __asm
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        halt
        __endasm;
      }
    }
  }
}
