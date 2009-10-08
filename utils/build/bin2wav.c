/*
 * bin2wav: a crude hack to generate a .WAV file from a binary file. The .WAV
 *          file includes a short BASIC loader that loads the following code
 *          to address 32768, and executes it from that address.
 *
 * The resulting file is suitable for loading into a Sinclair ZX Spectrum
 * using a music player (e.g., iPod) connected to the EAR socket.
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

#define SAMPLES_PER_SECOND        (44100)
#define TSTATES_PER_SECOND        (3500000)

#define LOW                       ('\000')
#define HIGH                      ('\377')

/*
 * Tape header layout, see
 * http://www.worldofspectrum.org/faq/reference/48kreference.htm
 */
#define SIZEOF_SPECTRUM_HEADER    (17)

#define HEADER_PROGRAM            (0)
#define HEADER_CODE               (3)

#define HEADER_OFFSET_FILETYPE    (0)
#define HEADER_OFFSET_FILENAME    (1)
#define HEADER_OFFSET_FILELENGTH  (11)
#define HEADER_OFFSET_PARAM1      (13)
#define HEADER_OFFSET_PARAM2      (15)

#define BITS0TO7(x)               ((x) & 0xffu)
#define BITS8TO15(x)              (((x) >> 8) & 0xffu)
#define BITS16TO23(x)             (((x) >> 16) & 0xffu)
#define BITS24TO31(x)             (((x) >> 24) & 0xffu)

/* ------------------------------------------------------------------------- */

static FILE *file           = NULL;

static uint32_t nbr_samples = 0;
static uint32_t nbr_tstates = 0;

static uint8_t infile_buffer[65536];    /* buffer for input data file */

/* ------------------------------------------------------------------------- */

static void
write_preliminary_header(void)
{
  /*
   * Preliminary file header: no length yet, will be added later
   */
  fprintf(file, "RIFFxxxxWAVE");
  
  /*
   * Format chunk: uncompressed PCM, 1 channel, 44.1kHz, 8 bits
   * values taken from http://www.sonicspot.com/guide/wavefiles.html
   *
   * Need to feed the zeros separately, NUL characters otherwise
   * terminate the string...
   */
  fprintf(file, "fmt "
          "\020%c%c%c"         /* 16 */
          "\001%c"             /* 1 */
          "\001%c"             /* 1 */
          "\104\254%c%c"       /* 44100 */
          "\104\254%c%c"       /* 44100 */
          "\001%c"             /* 1 */
          "\010%c",            /* 8 */
          0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  
  /*
   * Data chunk: no length yet, will be added later
   */
  fprintf(file, "datayyyy");
}

/* ------------------------------------------------------------------------- */

/*
 * Write given value to output, for the next 'tstates_duration' T-states
 */
static void
write_samples(uint8_t sample, uint32_t tstates_duration)
{
  nbr_tstates += tstates_duration;
  while(((nbr_samples + 1) / ((double) SAMPLES_PER_SECOND))
      <= (nbr_tstates / ((double) TSTATES_PER_SECOND)))
  {
    fputc(sample, file);
    nbr_samples ++;
  }
}

/* ------------------------------------------------------------------------- */

static void
write_pilot(uint8_t flag_byte)
{
  uint32_t pilot_cycles = (flag_byte & 0x80) ? 1611 : 4031;
  uint32_t i;
  
  for (i = 0; i < pilot_cycles; i++) {
    write_samples(HIGH, 2168);
    write_samples(LOW, 2168);
  }
  write_samples(HIGH, 667);
  write_samples(LOW, 735);
}

/* ------------------------------------------------------------------------- */

static void
write_byte(uint8_t byte)
{
  int i;
  for (i = 0; i < 8; i++) {
    uint32_t duration = (byte & 0x80) ? 1710 : 855;
    write_samples(HIGH, duration);
    write_samples(LOW, duration);
    byte <<= 1;
  }
}

/* ------------------------------------------------------------------------- */

static void
write_pause(uint32_t milliseconds)
{
  write_samples(HIGH, milliseconds * (TSTATES_PER_SECOND / 1000));
}

/* ------------------------------------------------------------------------- */

static void
write_block(uint8_t flag_byte, uint16_t data_length, const uint8_t *data)
{
  uint8_t  checksum = flag_byte;
  uint16_t i;
  
  write_pilot(flag_byte);
  write_byte(flag_byte);
  for (i = 0; i < data_length; i++) {
    checksum ^= data[i];
    write_byte(data[i]);
  }
  write_byte(checksum);
}

/* ------------------------------------------------------------------------- */

static void
write_header_block(uint8_t        file_type,
                   uint16_t       file_length,
                   const char    *file_name,
                   uint16_t       param1,
                   uint16_t       param2)
{
  static uint8_t speccy_header_prototype[SIZEOF_SPECTRUM_HEADER] = {
    0,                                        /* file type */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,             /* file name */
    0, 0,                                     /* file length */
    0, 0,                                     /* parameter 1 */
    0, 0                                      /* parameter 2 */
  };
  
  snprintf((char *) &speccy_header_prototype[HEADER_OFFSET_FILENAME],
           10,
           "%-10s",
           file_name);
  
  speccy_header_prototype[HEADER_OFFSET_FILETYPE]     = file_type;
  speccy_header_prototype[HEADER_OFFSET_FILELENGTH]   = BITS0TO7(file_length);
  speccy_header_prototype[HEADER_OFFSET_FILELENGTH+1] = BITS8TO15(file_length);
  speccy_header_prototype[HEADER_OFFSET_PARAM1]       = BITS0TO7(param1);
  speccy_header_prototype[HEADER_OFFSET_PARAM1+1]     = BITS8TO15(param1);
  speccy_header_prototype[HEADER_OFFSET_PARAM2]       = BITS0TO7(param2);
  speccy_header_prototype[HEADER_OFFSET_PARAM2+1]     = BITS8TO15(param2);
  
  write_block(0x00, SIZEOF_SPECTRUM_HEADER, speccy_header_prototype);
  write_pause(500);
}

/* ------------------------------------------------------------------------- */

static void
write_data_block(const uint8_t *file_data,
                 uint16_t       file_length)
{
  write_block(0xff, file_length, file_data);
  write_pause(1000);
}

/* ------------------------------------------------------------------------- */

static void
write_basic_loader(void)
{
  static uint8_t basic_loader[] = {
    0, 10,                              /* line 10 */
    0, 0,                               /* length of code below */
    253, '3', '2', '7', '6', '7',       /* CLEAR 32767 */
    14, 0, 0, 255, 127, 0,              /* integer 32767 */
    ':',                                /* : */
    239, '"', '"', 175,                 /* LOAD "" CODE */
    ':',                                /* : */
    249, 192, '3', '2', '7', '6', '8',  /* RANDOMIZE USR 32768 */
    14, 0, 0, 0, 128, 0,                /* integer 32768 */
    13,                                 /* ENTER */
    128                                 /* Sentinel: end of variable area */
  };

  /*
   * Fix line length field
   */
  basic_loader[2] = BITS0TO7(sizeof(basic_loader) - 5);
  basic_loader[3] = BITS8TO15(sizeof(basic_loader) - 5);
  
  write_header_block(HEADER_PROGRAM,
                     (uint16_t) sizeof(basic_loader),
                     "loader",
                     0x8000 /* no auto-start */,
                     (uint16_t) (sizeof(basic_loader) - 1));
  write_data_block(basic_loader, (uint16_t) sizeof(basic_loader));
}

/* ------------------------------------------------------------------------- */

static void
write_data_file(const char *filename)
{
  FILE *in_file = fopen(filename, "r");
  long bytes_read;

  if (in_file == NULL) {
    perror("opening input file");
    exit(1);
  }
  
  bytes_read = fread(infile_buffer,
                     sizeof(uint8_t),
                     sizeof(infile_buffer),
                     in_file);
  if (bytes_read < 0) {
    perror("reading input file");
    exit(1);
  }
  
  write_header_block(HEADER_CODE, (uint16_t) bytes_read, "code", 0x8000, 0x8000);
  write_data_block(infile_buffer, (uint16_t) bytes_read);
}

/* ------------------------------------------------------------------------- */

static void
complete_file(void)
{
  uint32_t file_length = ftell(file);
  uint32_t file_length_in_wav_header = file_length - 8;
  uint32_t data_chunk_length = file_length - 44;

  /*
   * Length field in RIFF header
   */
  fseek(file, 4, SEEK_SET);
  fprintf(file, "%c%c%c%c",
          BITS0TO7(file_length_in_wav_header),
          BITS8TO15(file_length_in_wav_header),
          BITS16TO23(file_length_in_wav_header),
          BITS24TO31(file_length_in_wav_header));

  /*
   * Length field in data chunk
   */
  fseek(file, 40, SEEK_SET);
  fprintf(file, "%c%c%c%c",
          BITS0TO7(data_chunk_length),
          BITS8TO15(data_chunk_length),
          BITS16TO23(data_chunk_length),
          BITS24TO31(data_chunk_length));
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
  if (argc != 3) {
    fprintf(stderr, "usage: bin2wav <binary-file> <wav-file>\n");
    exit(1);
  }
  
  file = fopen(argv[2], "w");
  if (file == NULL) {
    perror("opening output file");
    exit(1);
  }
  
  write_preliminary_header();
  write_basic_loader();
  write_data_file(argv[1]);
  complete_file();
  
  fclose(file);
}
