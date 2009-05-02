/*
 * Module tftp_fake:
 *
 * Faked TFTP implementation, loading blocks of 128K RAM rather than TFTP
 * (useful for testing snapshot loading)
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

#include "tftp.h"

/* ------------------------------------------------------------------------- */

#define BLOCK_SIZE                (512)

#define PAGE_REG                  (0x7FFD)

/*
 * Default page for 0xc000..0xffff
 */
#define DEFAULT_PAGE              (0)

#define BLOCKS_PER_PAGE           (0x4000 / BLOCK_SIZE)
#define ADDR_OF_BLOCK(n)          (0xc000 + ((n) * BLOCK_SIZE))

/* ------------------------------------------------------------------------- */

uint8_t fake_tftp_block_buf[BLOCK_SIZE];

/*
 * This thing is referenced (and increased) by crt0.asm. Normally lives in
 * eth.c, put here to avoid link errors.
 */
uint8_t timer_tick_count;

/* ------------------------------------------------------------------------- */

/*static*/ void transfer_block(uint8_t page, uint16_t addr)
__naked
{
  /*
   * assume page in (SP + 2)
   * assume offset in (SP + 3) .. (SP + 4)
   */
  
  (void) page, addr;
  
  __asm
  
    ld  hl, #2
    add hl, sp
    ld  a, (hl)
  
    inc hl
    ld  e, (hl)
    inc hl
    ld  d, (hl)
    ex  de, hl
  
    ;; now A holds requested bank
    ;; HL points to address to copy from
  
    ld  bc, #PAGE_REG
    out (c), a
    
    ld  de, #_fake_tftp_block_buf
    ld  bc, #BLOCK_SIZE
    ldir
    
    ld  bc, #PAGE_REG
    ld  a,  #DEFAULT_PAGE
    out (c), a
  
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
tftp_read_request(const char *filename)
{
  static const uint8_t pages_with_image[] = {3, 4, 6, 7};
  uint8_t i;
  
  (void) filename;
  for (i = 0; i < sizeof(pages_with_image); i++) {
    uint8_t page = pages_with_image[i];
    uint8_t j;
    for (j = 0; j < BLOCKS_PER_PAGE; j++) {
      transfer_block(page, ADDR_OF_BLOCK(j));
      NOTIFY_TFTP_DATA(fake_tftp_block_buf, BLOCK_SIZE, false);
    }
  }
  
  fatal_error(FATAL_ERROR_END_OF_DATA);
}
