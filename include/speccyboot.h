/*
 * Speccyboot hardware-related definitions.
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

#ifndef SPECCYBOOT_SPECCYBOOT_INCLUSION_GUARD
#define SPECCYBOOT_SPECCYBOOT_INCLUSION_GUARD

#include <stdint.h>

#include "spectrum.h"

/* -------------------------------------------------------------------------
 * I/O ports implemented by SpeccyBoot
 * ------------------------------------------------------------------------- */

Z80_PORT(0x009f) sbt_cfg_port;        /* speccyboot config */

/* -------------------------------------------------------------------------
 * Values of bits in OUT register
 * ------------------------------------------------------------------------- */

enum speccyboot_out_port_bit_t {
  BIT_SPI_SCK   = 0,
  BIT_UNUSED_1  = 1,
  BIT_UNUSED_2  = 2,
  BIT_SPI_CS    = 3,    /* CS pin for ENC28J60 */
  BIT_FRAM_A14  = 4,    /* Address bit A14 for FRAM (bank select) */
  BIT_FRAM_CS   = 5,    /* 0=enable FRAM, 1=disable FRAM */
  BIT_SPI_RST   = 6,    /* RESET pin for ENC28J60 */
  BIT_SPI_MOSI  = 7     /* SPI data to ENC28J60 */
};

#define SPI_SCK              (1 << BIT_SPI_SCK)
#define SPI_CS               (1 << BIT_SPI_CS)
#define SPI_RST              (1 << BIT_SPI_RST)
#define SPI_MOSI             (1 << BIT_SPI_MOSI)

#define SELECT_INTERNAL_ROM  (1 << BIT_FRAM_CS)
#define SELECT_FRAM_BANK_2   (1 << BIT_FRAM_A14)

/* -------------------------------------------------------------------------
 * Values of bits in IN register
 * ------------------------------------------------------------------------- */

enum speccyboot_in_port_bit_t {
  BIT_SPI_MISO  = 0,    /* SPI data from ENC28J60 */
  BIT_EN_WOL    = 1,    /* WOL pin from ENC28J60 */    
  BIT_EN_INT    = 2     /* INT pin from ENC28J60 */
};

#define SPI_MISO             (1 << BIT_SPI_MISO)

#define EN_WOL               (1 << BIT_EN_WOL)
#define EN_INT               (1 << BIT_EN_INT)

/* -------------------------------------------------------------------------
 * SPI buffers
 * ------------------------------------------------------------------------- */

#define SPI_CHUNK_SIZE    (128)
#define SPI_BUFFER_ADDR   (0x5B00)

/* ------------------------------------------------------------------------- */

#endif /* SPECCYBOOT_SPECCYBOOT_INCLUSION_GUARD */
