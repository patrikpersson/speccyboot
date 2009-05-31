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

#include "platform.h"

/* -------------------------------------------------------------------------
 * Values of bits in OUT register
 * ------------------------------------------------------------------------- */

#if 0
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
#endif

#define EN_CS               (0x08)
#define FRAM_CS             (0x20)
#define EN_RST              (0x40)

/* -------------------------------------------------------------------------
 * Values of bits in IN register
 * ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */

#endif /* SPECCYBOOT_SPECCYBOOT_INCLUSION_GUARD */
