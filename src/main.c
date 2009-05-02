/*
 * Module main:
 *
 * Present a menu to the user, and act on the selection made.
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

#include "platform.h"
#include "util.h"
#include "netboot.h"

/* ------------------------------------------------------------------------- */

#define KEYBOARD_ROW_ADDRESS          (0xBFFE)
#define KEY_ENTER                     (0x01)
#define KEY_J                         (0x08)

#define KEY_IS_PRESSED(status, key)   (((status) & (key)) == 0)

/* ========================================================================= */

void main(void) {
  
  set_screen_attrs(INK(BLACK) | PAPER(BLACK));
  set_border(BLACK);

  display_splash();

  set_attrs(INK(GREEN) | PAPER(BLACK), 8, 0, 32);
  set_attrs(INK(GREEN) | PAPER(BLACK) | BRIGHT, 9, 0, 32);
  set_attrs(INK(GREEN) | PAPER(BLACK), 10, 0, 32);
  
  set_attrs(INK(YELLOW) | PAPER(BLACK) | BRIGHT, 15, 0, 3);
  set_attrs(INK(WHITE) | PAPER(BLACK), 15, 3, 29);
  set_attrs(INK(YELLOW) | PAPER(BLACK) | BRIGHT, 15, 22, 4);
  
  for (;;) {
    static Z80_PORT(0xBFFE) keyboard_row;   /* keys H through ENTER */
    
    uint8_t keyboard_status = Z80_PORT_READ(keyboard_row);
    
    if (KEY_IS_PRESSED(keyboard_status, KEY_ENTER)) {
      __asm  
      
        ;; Stores the instruction 'out (0x9F), a' at 0xFFFE. Jumping there will
        ;; page in ROM 0, and then continue to execute into it.
        
        di
        ld  hl,   #0xFFFE
        ld  (hl), #0xD3     ;; out (N), a
        inc hl
        ld  (hl), #0x9F     ;; N for out instruction above
        ld  a,    #0x20     ;; select internal ROM
        jp  0xFFFE
      
      __endasm;
    }
    if (KEY_IS_PRESSED(keyboard_status, KEY_J)) {
      set_attrs(INK(BLACK) | PAPER(BLACK), 15, 0, 32);
      netboot_do();
    }
  }
}
