/*
 * Module spi:
 *
 * Bit-banged SPI access.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2012, Patrik Persson & Imrich Kolkol
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

#ifndef SPECCYBOOT_SPI_INCLUSION_GUARD
#define SPECCYBOOT_SPI_INCLUSION_GUARD

#include "spi_asm.h"

/* ------------------------------------------------------------------------- */

/*
 * Note the funny notation for 'rl x' as 'rl a, x' below:
 *
 * Apparently as-z80 got the 'rl x' instruction mixed up into the same group
 * as 'add x', 'cp x', and so on, and somehow thinks 'a' is an implicit
 * operand. We need to give the full version of the instruction for as-z80 to
 * figure things out in macros, because these will expand to a single line,
 * which takes some intelligence to parse correctly -- hence the use of
 * 'rl a, x' below.
 *
 * I got the idea of 'rl a, x' from here:
 * http://shop-pdp.kent.edu/ashtml/asz80.htm
 */

#pragma save
#pragma sdcc_hash +

/*
 * Write bit 7 from register REG to SPI MOSI, and then shift REG left one bit
 */
#define SPI_WRITE_BIT_FROM(REG)           \
  ld    a, #SPI_IDLE+SPI_IDLE             \
  rl    a, REG                            \
  rra                                     \
  out   (SPI_OUT), a                      \
  inc   a                                 \
  out   (SPI_OUT), a

/*
 * Shift register REG left one bit, and shift in a bit from SPI MISO to bit 0
 */
#define SPI_READ_BIT_TO(REG)              \
  ld    a, #SPI_IDLE                      \
  out   (SPI_OUT), a                      \
  inc   a                                 \
  out   (SPI_OUT), a                      \
  in    a, (SPI_IN)                       \
  rra                                     \
  rl    a, REG

#pragma restore

/* ========================================================================= */

/*
 * Read 8 bits from SPI to register C.
 * Destroys BC & AF; B will be zero on exit.
 */
void
spi_read_byte(void);

/* ------------------------------------------------------------------------- */

/*
 * Write 8 bits to SPI from register C.
 * Destroys BC & AF; B will be zero on exit.
 */
void
spi_write_byte(void);

#endif /* SPECCYBOOT_SPI_INCLUSION_GUARD */
