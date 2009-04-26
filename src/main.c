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

#include <stddef.h>
#include <stdbool.h>

#include "spectrum.h"
#include "speccyboot.h"
#include "netboot.h"

/* ========================================================================= */

/*
 * Code snippet to disable interrupts, switch FRAM/ROM banks, and jump to
 * the start of ROM.
 *
 * NOTE: this is NOT const, because it has to reside in RAM (or it will be
 * paged out by itself)!
 */
static uint8_t trampoline_code[] = {
  0xF3,             /* di */
  0x3E, 0x20,       /* ld a, 0x20 (select internal ROM) */
  0xD3, 0x9F,       /* out (0x9F), a */
  0xC3, 0x00, 0x00  /* jp 0x0000 */
};

/* ========================================================================= */

/* make some noise */
static void
make_noise(void)
__naked
{
  __asm
  
  ld  de, #0x1000
  ld  l, #128
noise_loop:
  ld  bc, #0xFFFE
  out (c), d
  
  ld b, #28
noise_l1:
  djnz  noise_l1
  
  out (c), e
  
  ld b, #28
noise_l2:
  djnz  noise_l2
  
  dec l
  jr  nz, noise_loop
  
  ret
  
  __endasm;
}

/* ========================================================================= */

void main(void) {
  uint8_t option = 0;
  bool redraw_screen = true;   /* Set if screen needs to be re-drawn */
  
  spectrum_init_font();
  spectrum_cls(INK(BLACK) | PAPER(BLACK), BLACK);

  /*
   * Display splash screen
   */
  __asm
  
    ld  hl, #_splash_screen
    ld  de, #0x4800
    
  main_splash_loop:
    ld  a, d
    cp  #0x50
    jr  z, main_splash_done
    ld  a, (hl)
    inc hl
    or  a
    ;; jr  z, main_splash_seq
    ld  (de), a
    inc de
    jr  main_splash_loop
    
  main_splash_seq:
    ld  b, (hl)
  main_splash_seq_loop:
    ld  (de), a
    inc de
    djnz  main_splash_seq_loop
    jr  main_splash_loop
    
  main_splash_done:
    
  __endasm;
  
  spectrum_set_attrs(INK(GREEN) | PAPER(BLACK), 8, 0, 32);
  spectrum_set_attrs(INK(GREEN) | PAPER(BLACK) | BRIGHT,          9, 0, 32);
  spectrum_set_attrs(INK(GREEN) | PAPER(BLACK), 10, 0, 32);
  
  spectrum_set_attrs(INK(WHITE) | PAPER(BLACK), 15, 0, 32);
  spectrum_set_attrs(INK(YELLOW) | PAPER(BLACK) | BRIGHT, 15, 0, 3);
  spectrum_set_attrs(INK(YELLOW) | PAPER(BLACK) | BRIGHT, 15, 22, 4);
  
  __asm
    ld  bc, #0xBFFE
  main_wait_key:
    in  d, (c)
    ld  a, d
    and #0x01           ;; ENTER
    jp  z, _trampoline_code
    ld  a, d
    and #0x08           ;; J "LOAD"
    jp  z, _netboot_do
    jr main_wait_key
  
  __endasm;
  
#if 0
  for (;;) {
    uint8_t new_option;
    enum spectrum_input_t user_input;
    
    if (redraw_screen) {     
      uint8_t i;
      
      spectrum_cls(INK(BLUE) | PAPER(WHITE), BLUE);
      display_splash();
      
      for (i = 0; i < NBR_OF_OPTIONS; i++) {
        spectrum_print_at(8 + (i << 1), 7, menu_options[i].title, NULL);
      }

      redraw_screen = false;
    }
    
    new_option = option;
    
    spectrum_set_attrs(INK(BLACK) | PAPER(YELLOW) | BRIGHT,
                       8 + (option << 1), 5, 22);
    
    user_input = spectrum_wait_input();
    
    switch (user_input) {
      case INPUT_UP:
        if (option > 0) {
          new_option = option - 1;
        }
        else {
          /* yell */
        }
        break;
      case INPUT_DOWN:
        if (option < (NBR_OF_OPTIONS - 1)) {
          new_option = option + 1;
        }
        else {
          /* yell */
        }
        break;
      case INPUT_FIRE:
        menu_options[option].handler();
        redraw_screen = true;
        break;
    }
    
    if (new_option != option && !redraw_screen) {
      spectrum_set_attrs(INK(BLUE) | PAPER(WHITE),
                         8 + (option << 1), 5, 22);
    }
    
    option = new_option;
  }
#endif
}
