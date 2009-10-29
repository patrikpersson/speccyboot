/*
 * Module spi:
 *
 * Bit-banged SPI access.
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

#include "spi.h"

/* ------------------------------------------------------------------------- */

uint8_t
spi_read_byte(void)
__naked
{
  __asm
   
    SPI_READ_TO(L)
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
spi_write_byte(uint8_t x)
__naked
{
  (void) x;       /* silence warning about argument 'x' not used */

  __asm
  
    ;; assumes x to be passed in (sp + 2)
    
    ld  hl, #2
    add hl, sp
    ld  e, (hl)
    
    SPI_WRITE_FROM(E)
    
    ret
  
  __endasm;
}
