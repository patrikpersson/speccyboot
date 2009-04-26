/*
 * xbm2speccy: translate a 256x64 XBM into a format that can easily be
 *             de-compressed and display by the Spectrum.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define IMAGE_WIDTH         (256)
#define IMAGE_HEIGHT        (64)
#define IMAGE_SIZE_BYTES    ((IMAGE_WIDTH * IMAGE_HEIGHT) / 8)
#define IMAGE_ROW_BYTES     (IMAGE_WIDTH / 8)

/* -------------------------------------------------------------------------
 * Make sure preprocessor is fed with the right data
 * ------------------------------------------------------------------------- */

#ifndef XBM_SOURCEFILE
#error  XBM_SOURCEFILE must be set to the XBM file to convert
#endif


#include XBM_SOURCEFILE


#if (speccyboot_width != IMAGE_WIDTH) || (speccyboot_height != IMAGE_HEIGHT)
#error Invalid image size (expected 256x64)
#endif

/* -------------------------------------------------------------------------
 * Buffer for bitmap data, in Spectrum-style order
 * ------------------------------------------------------------------------- */

static uint8_t uncompressed_speccy_buf[IMAGE_SIZE_BYTES];

/* ------------------------------------------------------------------------- */

/*
 * The XBM bits are bit-flipped, compared to what the Spectrum expects.
 */
#define FLIP_BITS(x)      (  ((x & 0x01u) << 7)                               \
                           | ((x & 0x02u) << 5)                               \
                           | ((x & 0x04u) << 3)                               \
                           | ((x & 0x08u) << 1)                               \
                           | ((x & 0x10u) >> 1)                               \
                           | ((x & 0x20u) >> 3)                               \
                           | ((x & 0x40u) >> 5)                               \
                           | ((x & 0x80u) >> 7) )

/* ------------------------------------------------------------------------- */

/*
 * write NUL N, where N denotes the number of NULs. If N is 0, it should
 * be interpreted as 256.
 */
void write_untranslated_byte(uint8_t b)
{
  static uint16_t nbr_bytes = 0;
  
  if (nbr_bytes != 0)         printf(", ");
  if ((nbr_bytes % 8) == 0)   printf("\n");
  
  nbr_bytes ++;
  
  printf("0x%02x", (int) b);
}

/* ------------------------------------------------------------------------- */

/*
 * length of current sequence of NULs
 * if zero, no compression is currently taking place
 */
static uint8_t run_length = 0;

static void
finish_sequence(void)
{
  if (run_length) {
    write_untranslated_byte(0);
    write_untranslated_byte(run_length);
    run_length = 0;
  }
}

/* ------------------------------------------------------------------------- */

/*
 * Write one byte of data; handles compression
 */
void write_byte(uint8_t b)
{
  if (1 || b) {
    finish_sequence();
    write_untranslated_byte(b);
  }
  else {              /* another NUL in an ongoing compressed sequence */
    run_length ++;
    if (run_length == 0) {
      finish_sequence();
    }
  }
}

/* ------------------------------------------------------------------------- */

int main(void) {
  /*
   * Re-order bytes to Spectrum style
   */
  {
    uint16_t r;
    for (r = 0; r < IMAGE_HEIGHT; r++) {
      uint16_t c;
      for (c = 0; c < IMAGE_ROW_BYTES; c++) {
        uint16_t dest_row_offset = ((r & 0x07) * 0x0100) + ((r >> 3) * 0x020);
        uint8_t bits = speccyboot_bits[r * IMAGE_ROW_BYTES + c];
        uncompressed_speccy_buf[dest_row_offset + c] = FLIP_BITS(bits);
      }
    }
  }
  
  /*
   * Write as binary, as an absolute-addressed definition for SDCC
   */
  {
    printf("const unsigned char splash_screen[] = {");
    uint16_t i;
    for (i = 0; i < IMAGE_SIZE_BYTES; i++) {
      write_byte(uncompressed_speccy_buf[i]);
    }
    finish_sequence();      /* just to be sure */
    printf("};\n");
  }
  
  exit(0);
}
