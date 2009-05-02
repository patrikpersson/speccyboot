/*
 * Module logging:
 *
 * Diagnostic output, line-by-line.
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

#include <stdint.h>

#include "logging.h"
#include "util.h"
#include "platform.h"
#include "speccyboot.h"

/* ------------------------------------------------------------------------- */

#define BITMAP_BASE   (0x4000)
#define BITMAP_SIZE   (0x1800)
#define ATTRS_BASE    ((BITMAP_BASE) + (BITMAP_SIZE))
#define ATTRS_SIZE    (0x300)

#define ROW_LENGTH    (32)

static uint8_t copy_font_code[] = {
  0xF3,                       /* di */
  0x3E, FRAM_CS + EN_CS,      /* ld a, #0x28: page out FRAM, disable Ethernet */
  0xD3, 0x9F,                 /* out (0x9f), a */
  0x01, 0xFD, 0x7F,           /* ld bc, #0x7FFD */
  0x3E, 0x10,                 /* ld a, #0x10: page in 128k ROM 1 (48k BASIC) */
  0xED, 0x79,                 /* out (c), a */
  0x21, 0x00, 0x3D,           /* ld hl, #0x3D00 */
  0x11, 0x00, 0x00,           /* ld de, #0xXXYY */
  0x01, 0x00, 0x03,           /* ld bc, #0x0300 */
  0xED, 0xB0,                 /* ldir */
  0x3E, EN_CS,                /* ld a, #0x08: page in FRAM, disable Ethernet */
  0xD3, 0x9F,                 /* out (0x9f), a */
  0x01, 0xFD, 0x7F,           /* ld bc, #0x7FFD */
  0xED, 0x79,                 /* out (c), a */
  0xFB,                       /* ei */
  0xC9                        /* ret */
};

#define OFFSET_OF_FONT_ADDR_LSB     (16)
#define OFFSET_OF_FONT_ADDR_MSB     (17)

#define NBR_OF_CHARS                (96)      /* 96 chars (32..127) */
#define BYTES_PER_CHAR              (8)

static uint8_t fontdata[NBR_OF_CHARS * BYTES_PER_CHAR];

/* ------------------------------------------------------------------------- */

/*
 * Simple hex conversion
 */
#define hexdigit(n) ( ((n) < 10) ? ((n) + '0') : ((n) + 'a' - 10))

/* ------------------------------------------------------------------------- */

static void
print_char_at(uint8_t ch, uint8_t *dst_p)
{
  uint8_t *src_p = &fontdata[(ch - ' ') << 3];
  uint16_t i;
  for (i = 0; i < 0x0800; i += 0x0100) {
    dst_p[i] = *src_p++;
  }
}
  
/* ------------------------------------------------------------------------- */

static void
print_at(uint8_t row, uint8_t col,
         const char *str, const uint8_t *args)
{
  uint8_t *dst_p = ((uint8_t *) BITMAP_BASE)
  + ((row & 0x18) << 8) + ((row & 0x07) << 5) + col;
  
  while (col < ROW_LENGTH && *str) {
    uint8_t ch = *str++;
    
    switch (ch) {
      case DEC8_ARG_CHAR:
      {
        uint8_t arg = *args++;
        if (arg >= 100) {
          print_char_at('0' + (arg / 100), dst_p++);
          col++;
          arg %= 100;
        }
        if (arg >= 10 && col < ROW_LENGTH) {
          print_char_at('0' + (arg / 10), dst_p++);
          col++;
          arg %= 10;
        }
        if (col < ROW_LENGTH) {
          print_char_at('0' + arg, dst_p++);
          col++;
        }
        break;
      }
      case HEX16_ARG_CHAR:
      {
        uint8_t lobyte = *args++;
        uint8_t hibyte = *args++;
        print_char_at(hexdigit(hibyte >> 4), dst_p++);
        col++;
        if (col >= ROW_LENGTH)  break;
        
        print_char_at(hexdigit(hibyte & 0x0f), dst_p++);
        col ++;
        if (col >= ROW_LENGTH)  break;
        
        print_char_at(hexdigit(lobyte >> 4), dst_p++);
        col ++;
        if (col >= ROW_LENGTH)  break;
        
        print_char_at(hexdigit(lobyte & 0x0f), dst_p++);
        col ++;
        break;
      }
      case HEX8_ARG_CHAR:
      {
        uint8_t arg = *args++;
        print_char_at(hexdigit(arg >> 4), dst_p++);
        col++;
        if (col >= ROW_LENGTH)  break;
        print_char_at(hexdigit(arg & 0x0f), dst_p++);
        col ++;
        break;
      }
      default:
        print_char_at(ch, dst_p++);
        col ++;
        break;
    }
    
  }
}

/* ------------------------------------------------------------------------- */

static void
scroll(void)
{
  _asm
  ld   hl, #(BITMAP_BASE + 0x0020)
  ld   de, #BITMAP_BASE
  ld   bc, #0x07e0
  ldir
  
  ld   a,  #0x40
lp1:
  ld   h,  a  
  set  3,  h
  ld   l,  #0
  ; hl is 0x4n00 where n is 8..f
  ld   d,  a
  ld   e,  #0xe0
  ; de is 0x4ne0 where n is 0..7
  ld   bc, #0x0020
  ldir
  inc  a
  cp   #0x48
  jr   nz,lp1
  
  ld   hl, #(BITMAP_BASE + 0x0820)
  ld   de, #(BITMAP_BASE + 0x0800)
  ld   bc, #0x07e0
  ldir
  
lp2:
  ld   h,  a  
  set  #4,  h
  res  #3,  h
  ld   l,  #0
  ; hl is 0x5n00 where n is 0..8
  ld   d,  a
  ld   e,  #0xe0
  ; de is 0x4ne0 where n is 8..f
  ld   bc, #0x0020
  ldir
  inc  a
  cp   #0x50
  jr   nz,lp2
  
  ld   hl, #(BITMAP_BASE + 0x1020)
  ld   de, #(BITMAP_BASE + 0x1000)
  ld   bc, #0x07e0
  ldir
  
  ld   e,    #0
  ld   h,    #0x50
lp4:
  ld   b,    #32
  ld   l,    #0xe0
lp3:
  ld   (hl), e
  inc  hl
  djnz lp3
  ld   a, h
  cp   #0x58
  jr   nz, lp4
  
  _endasm;
}

/* ------------------------------------------------------------------------- */

void
logging_init(void)
{
  set_screen_attrs(INK(GREEN) | PAPER(BLACK));
  
  copy_font_code[OFFSET_OF_FONT_ADDR_LSB] = (((uint16_t) &fontdata) & 0xff);
  copy_font_code[OFFSET_OF_FONT_ADDR_MSB] = ((((uint16_t) &fontdata) >> 8) & 0xff);
  JUMP_TO(copy_font_code);

  logging_add_entry("Logging started", 0);
}

/* ------------------------------------------------------------------------- */

void
logging_add_entry(const char *msg, const uint8_t *args)
{
  scroll();
  print_at(22, 0, msg, args);
}
