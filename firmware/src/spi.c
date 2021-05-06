/*
 * Module spi:
 *
 * Bit-banged SPI access.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
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

#include "spi.h"

/* ------------------------------------------------------------------------- */

uint8_t
spi_read_byte(void)
__naked
{
  __asm

    ld   b, #8
spi_read_byte_loop::
    SPI_READ_BIT_TO(C)
    djnz spi_read_byte_loop

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
spi_write_byte(uint8_t x)
__naked
{
  (void) x;

  __asm

    ld    b, #8
spi_write_byte_loop::
    SPI_WRITE_BIT_FROM(c)
    djnz  spi_write_byte_loop

    ret

  __endasm;
}
