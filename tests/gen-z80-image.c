/*
 * gen-z80-image:
 *
 * Reads raw data on standard input, and writes a .z80 snapshot of the
 * indicated type on standard output.
 *
 * See http://www.worldofspectrum.org/faq/reference/z80format.htm
 * 
 * Usage:
 *   gen-z80-image <snapshot-type>
 *
 * See below for an explanation of snapshot-type.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "register-values.h"

#define PAGE_SIZE         (0x4000)
#define DATA_LENGTH       (4 * PAGE_SIZE)

#define DATA_48K_LENGTH   (3 * PAGE_SIZE)

#define Z80_FLAG_BYTE     (0xED)

static uint8_t buffer[DATA_LENGTH];
static uint8_t compression_buffer[DATA_LENGTH];

/* ------------------------------------------------------------------------- */

/*
 * Compresses data; returns number of bytes in destination, or 0xffff
 * if no compression was made
 */
static uint16_t
compress_data(const uint8_t *src_data,
              uint8_t       *dst_buf,
              uint16_t       nbr_src_bytes)
{
  uint16_t i;
  uint16_t dst_idx = 0;
  
  fprintf(stderr, "encoding %d bytes\n", nbr_src_bytes);
  
  for (i = 0; i < nbr_src_bytes; i++) {
    
    if (dst_idx > nbr_src_bytes) {
      return 0xffff;
    }

    if ( ( (i < (nbr_src_bytes - 1))
            && (src_data[i]   == Z80_FLAG_BYTE)
            && (src_data[i+1] == Z80_FLAG_BYTE))
      || ( (i < (nbr_src_bytes - 4))         /* minimal encoded length */
            && (src_data[i] == src_data[i+1])
            && (src_data[i] == src_data[i+2])
            && (src_data[i] == src_data[i+3])
            && (src_data[i] == src_data[i+4])) )
    {
      uint8_t n = 1;
      while (((i + n) < nbr_src_bytes)
             && (src_data[i + n] == src_data[i])
             && (n != 0xff))
      {
        n ++;
      }
      
      if ((dst_idx + 4) > nbr_src_bytes) {
        return 0xffff;
      }
      
      dst_buf[dst_idx ++] = Z80_FLAG_BYTE;
      dst_buf[dst_idx ++] = Z80_FLAG_BYTE;
      dst_buf[dst_idx ++] = n;
      dst_buf[dst_idx ++] = src_data[i];
      
      i += n - 1;
    }
    else {
      dst_buf[dst_idx ++] = src_data[i];
    }
  }

  if (dst_idx == nbr_src_bytes) {
    return 0xffff;
  }

  return dst_idx;
}

/* ------------------------------------------------------------------------- */

static void
write_page_uncompressed(uint8_t page_id, const uint8_t *page_data)
{
  size_t write_status;
  
  fputc(0xff, stdout);
  fputc(0xff, stdout);
  fputc(page_id, stdout);
  
  write_status = fwrite(page_data, 1, PAGE_SIZE, stdout);
  assert(write_status == PAGE_SIZE);
}

/* ------------------------------------------------------------------------- */

static void
write_page_compressed(uint8_t page_id, const uint8_t *page_data)
{
  size_t write_status;
  
  uint16_t bytes_out = compress_data(page_data,
                                     compression_buffer,
                                     PAGE_SIZE);
  
  fputc((bytes_out & 0x00ff), stdout);
  fputc((bytes_out >> 8), stdout);
  fputc(page_id, stdout);
  
  fprintf(stderr, "compressed page %d to %d bytes\n", page_id, bytes_out);
  
  if (bytes_out == 0xffff) {
    write_status = fwrite(page_data, 1, PAGE_SIZE, stdout);
    assert(write_status == PAGE_SIZE);
  }
  else {
    write_status = fwrite(compression_buffer, 1, bytes_out, stdout);
    assert(write_status == bytes_out);
  }
}

/* ------------------------------------------------------------------------- */

static void
write_page_empty(uint8_t page_id)
{
  size_t write_status;
  uint16_t i;
  
  fputc(0xff, stdout);
  fputc(0xff, stdout);
  fputc(page_id, stdout);
  
  for (i = 0; i < PAGE_SIZE; i++) {
    fputc(0, stdout);
  }
}

/* ------------------------------------------------------------------------- */

static void write1(void)
{
  static const uint8_t header_v1[] = {
    REG_A, REG_F, REG_C, REG_B, REG_L, REG_H,
    0x00, 0x70,     /* pc */
    0x00, 0x74,     /* sp */
    REG_I, REG_R,
    0x00,             /* flags: no compression */
    REG_E, REG_D, REG_CP, REG_BP, REG_EP, REG_DP, REG_LP, REG_HP,
    REG_AP, REG_FP, REG_IY_LO, REG_IY_HI, REG_IX_LO, REG_IX_HI,
    0, 0,       /* IFF1-2 */
    1           /* IM1 */
  };

  size_t hdr_write_status = fwrite(header_v1, 1, sizeof(header_v1), stdout);
  size_t data_write_status = fwrite(buffer, 1, DATA_48K_LENGTH, stdout);
  assert(hdr_write_status == sizeof(header_v1));
  assert(data_write_status == DATA_48K_LENGTH);
}

/* ------------------------------------------------------------------------- */

static void write2(void)
{
  static const uint8_t header[] = {
    REG_A, REG_F, REG_C, REG_B, REG_L, REG_H,
    0x00, 0x70,     /* pc */
    0x00, 0x74,     /* sp */
    REG_I, REG_R,
    0x20,             /* flags: compression on */
    REG_E, REG_D, REG_CP, REG_BP, REG_EP, REG_DP, REG_LP, REG_HP,
    REG_AP, REG_FP, REG_IY_LO, REG_IY_HI, REG_IX_LO, REG_IX_HI,
    0, 0,       /* IFF1-2 */
    1           /* IM1 */
  };
  
  size_t hdr_write_status = fwrite(header, 1, sizeof(header), stdout);
  
  uint16_t bytes_out = compress_data(buffer,
                                     compression_buffer,
                                     DATA_48K_LENGTH);
  
  size_t data_write_status = fwrite(compression_buffer,
                                    1,
                                    bytes_out,
                                    stdout);
  
  assert(hdr_write_status == sizeof(header));
  assert(bytes_out != 0xffff);    /* this case requires compressible data */
  assert(data_write_status == bytes_out);
  
  /*
   * end marker
   */
  fputc(0, stdout);
  fputc(Z80_FLAG_BYTE, stdout);
  fputc(Z80_FLAG_BYTE, stdout);
  fputc(0, stdout);
}

/* ------------------------------------------------------------------------- */

static void write3(void)
{
  static const uint8_t header[] = {
    REG_A, REG_F, REG_C, REG_B, REG_L, REG_H,
    0x00, 0x00,     /* pc */
    0x00, 0x74,     /* sp */
    REG_I, REG_R,
    0x00,
    REG_E, REG_D, REG_CP, REG_BP, REG_EP, REG_DP, REG_LP, REG_HP,
    REG_AP, REG_FP, REG_IY_LO, REG_IY_HI, REG_IX_LO, REG_IX_HI,
    0, 0,       /* IFF1-2 */
    1,          /* IM1 */
    
    /* version 2 additional header follows */
    23, 0,      /* version 2 header, 23 bytes */
    0x00, 0x70, /* pc */
    0x00,       /* 48k Spectrum */
    0x00,       /* 128k banking state */
    0x00,       /* no Interface I */
    0x00,       /* flags */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* sound state */
  };
  
  size_t hdr_write_status = fwrite(header, 1, sizeof(header), stdout);
  assert(hdr_write_status == sizeof(header));
  write_page_compressed(5, &buffer[PAGE_SIZE * 2]);
  write_page_compressed(4, &buffer[PAGE_SIZE]);
  write_page_compressed(8, &buffer[0]);
}

/* ------------------------------------------------------------------------- */

static void write5(void)
{
  static const uint8_t header_v2_128[] = {
    REG_A, REG_F, REG_C, REG_B, REG_L, REG_H,
    0x00, 0x00,     /* pc */
    0x00, 0x00,     /* sp */
    0x00, 0x00,
    0x00,             /* flags: no compression */
    REG_E, REG_D, REG_CP, REG_BP, REG_EP, REG_DP, REG_LP, REG_HP,
    REG_AP, REG_FP, REG_IY_LO, REG_IY_HI, REG_IX_LO, REG_IX_HI,
    0, 0,       /* IFF1-2 */
    1,          /* IM1 */
    
    /* version 2 additional header follows */
    23, 0,      /* version 2 header, 23 bytes */
    0x00, 0x00, /* pc */
    0x03,       /* 128k Spectrum */
    0x00,       /* 128k banking state */
    0x00,       /* no Interface I */
    0x00,       /* flags */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /* sound state */
  };
  
  size_t hdr_write_status = fwrite(header_v2_128, 1, sizeof(header_v2_128), stdout);
  assert(hdr_write_status == sizeof(header_v2_128));
  
  /*
   * The page IDs written in the .z80 files are offset by 3 from the
   * Spectrum's numbering
   */
  write_page_empty(3);
  write_page_empty(4);
  write_page_empty(5);
  write_page_uncompressed(6, &buffer[0]);
  write_page_uncompressed(7, &buffer[PAGE_SIZE]);
  write_page_empty(8);
  write_page_uncompressed(9, &buffer[2 * PAGE_SIZE]);
  write_page_uncompressed(10, &buffer[3 * PAGE_SIZE]);
}

/*
 * Possible outputs:
 *
 * 1: 48k,  version 1, entry point 0x7000, uncompressed
 * 2: 48k,  version 1, entry point 0x7000, compressed
 * 3: 48k,  version 2, entry point 0x7000, uncompressed
 * 4: 48k,  version 2, entry point 0x7000, compressed
 * 5: 128k, version 2, entry point 0x0000,
 *          up to 64K of input written to '128 pages 3, 4, 6, 7
 *
 * Note that for cases where compressed data is written, the input must be
 * compressible (i.e., the .z80 compression algorithm should be able to
 * compress it).
 */
int main(int argc, char **argv)
{
  int z80_version;
  
  size_t read_status = fread(buffer, sizeof(uint8_t), DATA_LENGTH, stdin);
  assert(read_status > 0);
  
  assert(argc == 2);
  z80_version = atoi(argv[1]);
  assert(z80_version >= 1 && z80_version <= 5);

  switch(z80_version) {
    case 1:
      write1();
      break;
    case 2:
      write2();
      break;
    case 3:
      write3();
      break;
    case 4:
      // write4();
      break;
    case 5:
      write5();
      break;
  }
  
  exit(0);
}
