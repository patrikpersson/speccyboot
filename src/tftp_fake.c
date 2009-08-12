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
#define DEFAULT_PAGE              (1)

#define BLOCKS_PER_PAGE           (0x4000 / BLOCK_SIZE)
#define ADDR_OF_BLOCK(n)          (0xc000 + ((n) * BLOCK_SIZE))

/* ------------------------------------------------------------------------- */

static uint8_t fake_tftp_block_buf[BLOCK_SIZE];

/* ------------------------------------------------------------------------- */

/*
 * Picks a 512-byte block from a given address, with a given page at 0xc000.
 */
static void transfer_block(uint8_t page, uint16_t addr)
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
  bool is_snapshot = false;   /* true for snapshot, false for snapshot list */
  
  {
    char p;
    while (p = *filename++) {
      if (p == '.') {
        is_snapshot = (*filename == 'z');   /* crude */
        break;
      }
    };
  }
  
  if (is_snapshot) {
    for (i = 0; i < sizeof(pages_with_image); i++) {
      uint8_t page = pages_with_image[i];
      uint8_t j;
      for (j = 0; j < BLOCKS_PER_PAGE; j++) {
        transfer_block(page, ADDR_OF_BLOCK(j));
        NOTIFY_TFTP_DATA(fake_tftp_block_buf, BLOCK_SIZE, true);
      }
    }
    
    fatal_error("unexpected end of data");
  }
  else {
    static const char fake_menu_pt1[]  = 
    "A001.z80\n"
    "A002.z80\n"
    "A003.z80\n"
    "A004.z80\n"
    "A005.z80\n"
    "Arkanoid.z80\n"
    "B001.z80\n"
    "B002.z80\n"
    "B003.z80\n"
    "B004.z80\n"
    "B005.z80\n"
    "Bomb Jack.z80\n"
    "Bubble Bobble.z80\n"
    "C001.z80\n"
    "C002.z80\n"
    "C003.z80\n"
    "C004.z80\n"
    "C005.z80\n"
    "Carrier Command.z80\n"
    "Chuckie Egg.z80\n"
    "Dan Dare.z80\n";
    static const char fake_menu_pt2[]  = 
    "Dynamite Dan.z80\n"
    "Enduro Racer.z80\n"
    "Fighter Pilot.z80\n"
    "Flight Simulation.z80\n"
    "Frankie Goes To Hollywood.z80\n"
    "Gauntlet.z80\n"
    "Gunship.z80\n"
    "Jet Set Willy.z80\n"
    "Manic Miner.z80\n"
    "Paperboy.z80\n"
    "Pssst.z80\n"
    "Saboteur.z80\n"
    "Short Circuit - 48k.z80\n"
    "Silent Service.z80\n"
    "Strike Force Harrier.z80\n"
    "Tetris.z80\n"
    "Thanatos.z80\n"
    "The Great Escape.z80\n"
    "Wizball.z80\n"
    "Yie Ar Kung Fu.z80\n"
;
    NOTIFY_TFTP_DATA(fake_menu_pt1, sizeof(fake_menu_pt1), true);
    NOTIFY_TFTP_DATA(fake_menu_pt2, sizeof(fake_menu_pt2), false);
  }
}
