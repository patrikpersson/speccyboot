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
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stddef.h>
#include <stdbool.h>

#include "spectrum.h"
#include "speccyboot.h"
#include "netboot.h"
#include "params.h"
#include "timer.h"

/* ========================================================================= */

/*
 * Code snippet to disable interrupts, switch FRAM/ROM banks, and jump to
 * the start of ROM or FRAM.
 *
 * NOTE: this is NOT const, because it has to reside in RAM (or it will be
 * paged out by itself)!
 */
static uint8_t trampoline_code[] = {
  0xF3,             /* di */
  0x3E, 0x00,       /* ld a,X */
  0xD3, 0x9F,       /* out (0x9F), a */
  0xC3, 0x00, 0x00  /* jp 0x0000 */
};

/*
 * Position of the bank selector within the trampoline above (X)
 */
#define TRAMPOLINE_MASK_INDEX   (2)

/* ========================================================================= */

/* -------------------------------------------------------------------------
 * User selected 'standard boot'
 * ------------------------------------------------------------------------- */
static void
option_standard_boot(void)
{
  trampoline_code[TRAMPOLINE_MASK_INDEX] = SELECT_INTERNAL_ROM;
  
  JUMP_TO(trampoline_code);
}

/* -------------------------------------------------------------------------
 * User selected 'alternate boot'
 * ------------------------------------------------------------------------- */
static void
option_alternate_boot(void)
{
  trampoline_code[TRAMPOLINE_MASK_INDEX] = SELECT_FRAM_BANK_2;
  
  JUMP_TO(trampoline_code);
}

/* -------------------------------------------------------------------------
 * User selected 'system info'
 * ------------------------------------------------------------------------- */
static void
option_system_info(void)
{
  struct mac_address_t   mac_address;
  struct ipv4_address_t  host_address;
  struct ipv4_address_t  server_address;
  
  spectrum_cls(INK(BLUE) | PAPER(WHITE), WHITE);

  params_get_mac_address(&mac_address);
  spectrum_print_at(5, 2, BOLD_ON "MAC ADDRESS " BOLD_OFF
                          HEX_ARG ":" HEX_ARG ":" HEX_ARG ":" 
                          HEX_ARG ":" HEX_ARG ":" HEX_ARG,
                    mac_address.addr);

  params_get_host_address(&host_address);
  spectrum_print_at(7, 3, BOLD_ON "IP ADDRESS " BOLD_OFF
                          DEC_ARG "." DEC_ARG "." DEC_ARG "." DEC_ARG,
                    host_address.addr);
  
  params_get_server_address(&server_address);
  spectrum_print_at(9, 2, BOLD_ON "TFTP SERVER " BOLD_OFF
                          DEC_ARG "." DEC_ARG "." DEC_ARG "." DEC_ARG,
                    server_address.addr);
  
  spectrum_print_at(23, 0, BOLD_ON "COMPILED " BOLD_OFF __DATE__ " " __TIME__,
                    NULL);
  
  spectrum_wait_input();
}

/* -------------------------------------------------------------------------
 * Menu options, with corresponding functions to execute
 * ------------------------------------------------------------------------- */
static const struct {
  const char* const title;
  const void (*const handler)();
} menu_options[] = {
  { "Network boot", &netboot_do },
  { "Standard boot", &option_standard_boot },
  { "Alternate boot", &option_alternate_boot },
  { "System information", &option_system_info }
};

#define NBR_OF_OPTIONS ((sizeof(menu_options)) / (sizeof(menu_options[0])))

/* ========================================================================= */

void main(void) {
  uint8_t option = 0;
  bool redraw_screen = true;   /* Set if screen needs to be re-drawn */
  
  /*
   * Need to delay for about 200ms for the Spectrum 128k hardware to settle
   * after reset. If this is not done, memory paging behaves badly.
   */
  spectrum_cls(PAPER(BLACK), BLACK);
  timer_delay(SECOND / 5);
  spectrum_init_font();
  
  for (;;) {
    uint8_t new_option;
    enum spectrum_input_t user_input;
    
    if (redraw_screen) {     
      uint8_t i;
      
      spectrum_cls(INK(BLUE) | PAPER(WHITE), BLUE);
      spectrum_set_attrs(INK(YELLOW) | PAPER(BLUE), 23, 0, 10);
      spectrum_set_attrs(INK(WHITE) | PAPER(BLUE), 23, 10, 22);
      
      for (i = 0; i < NBR_OF_OPTIONS; i++) {
        spectrum_print_at(7 + (i << 1), 7, menu_options[i].title, NULL);
      }

      spectrum_print_at(23, 0, BOLD_ON "SPECCYBOOT"
                               BOLD_OFF " \177 2009 Patrik Persson", NULL);

      redraw_screen = false;
    }
    
    new_option = option;
    
    spectrum_set_attrs(INK(BLACK) | PAPER(YELLOW) | BRIGHT,
                       7 + (option << 1), 5, 22);
    
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
                         7 + (option << 1), 5, 22);
    }
    
    option = new_option;
  }
}
