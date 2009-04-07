#include <stddef.h>
#include <stdbool.h>

#include "spectrum.h"
#include "speccyboot.h"
#include "netboot.h"
#include "params.h"

/* ========================================================================= */

/*
 * Code snippet to disable interrupts, switch FRAM/ROM banks, and jump to
 * the start of ROM or FRAM.
 *
 * NOTE: this is NOT const, because it has to reside in RAM (or it will be
 * paged out by itself)!
 */
static uint8_t trampoline_code[] = {
  0x3E, 0x00,       /* ld a,X */
  0xD3, 0x9F,       /* out (0x9F), a */
  0xC3, 0x00, 0x00  /* jp 0x0000 */
};

/*
 * Position of the bank selector within the trampoline above (X)
 */
#define TRAMPOLINE_MASK_INDEX   (1)

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
  mac_address_t   mac_address;
  ipv4_address_t  host_address;
  ipv4_address_t  server_address;
  
  spectrum_cls(INK(BLUE) | PAPER(WHITE), WHITE);
#if 0
  params_get_mac_address(&mac_address);
  spectrum_print_at(5, 0, "MAC address: \200:\200:\200:\200:\200:\200",
                    mac_address[0], mac_address[1], mac_address[2],
                    mac_address[3], mac_address[4], mac_address[5]);
#endif
  spectrum_print_at(23, 0, "Build: " __DATE__ ", " __TIME__);
  
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
        spectrum_print_at(7 + (i << 1), 7, menu_options[i].title);
      }

      spectrum_print_at(23, 0, "SPECCYBOOT \177 2009 Patrik Persson");

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
