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
static uint16_t run_length = 0;

/* ------------------------------------------------------------------------- */

/*
 * A single NUL is written as NUL.
 * Two or more NULs are written as NUL NUL (n), where N is the number of NULs
 * minus two:
 * NUL NUL 0 <=> two NULs
 * NUL NUL 1 <=> three NULs
 * and so on.
 */
static void
flush_compression_sequence(void)
{
  if (run_length == 1) {
    write_untranslated_byte(0);
  }
  else if (run_length >= 2) {
    write_untranslated_byte(0);
    write_untranslated_byte(0);
    write_untranslated_byte(run_length - 2);
  }
  run_length = 0;
}

/* ------------------------------------------------------------------------- */

/*
 * Write one byte of data; handles compression
 */
void write_byte(uint8_t b)
{
  if (b) {
    if (run_length) {
      flush_compression_sequence();
    }
    write_untranslated_byte(b);
  }
  else {              /* another NUL in an ongoing compressed sequence */
    run_length ++;
    if (run_length == 0x0101) {     /* maximal number of NULs in one go */
      flush_compression_sequence();
    }
  }
}

/* ------------------------------------------------------------------------- */

int main(void) {
  /*
   * Write compressed data
   */
  {
    uint16_t i;

    printf("#include <stdint.h>\nconst uint8_t splash_screen[] = {");
    for (i = 0; i < IMAGE_SIZE_BYTES; i++) {
      write_byte(FLIP_BITS(speccyboot_bits[i]));
    }
    
    flush_compression_sequence();
    printf("};\n");
  }
  
  exit(0);
}
