/*
 * Module menu:
 *
 * Display a menu from the loaded snapshot file, and load selected snapshot.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
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

#include "menu.h"

#include "globals.h"
#include "ui.h"
#include "util.h"
#include "z80_loader.h"

#pragma codeseg NONRESIDENT

/* ------------------------------------------------------------------------- */

/* Number of snapshot names displayed at a time */
#define DISPLAY_LINES     (20)

/* ------------------------------------------------------------------------- */

void
run_menu(void)
{
  unsigned char *src = &snapshot_list;
  uint16_t nbr_snapshots = 0;

  print_at(23, 0, '\0', "Local:           TFTP:");
  set_attrs(INK(BLACK) | PAPER(BLACK), 0, 16, 10);
  set_attrs(INK(WHITE) | PAPER(BLACK), 23, 0, 31);
  print_ip_addr( &ip_config.host_address, (uint8_t *) LOCAL_IP_POS);
  print_ip_addr( &ip_config.tftp_server_address, (uint8_t *) SERVER_IP_POS);

#ifdef STAGE2_IN_RAM
  if (curr_write_pos == &snapshot_list) {
    tftp_read_request("snapshots.lst");
    __asm
      jp   main_loop
    __endasm;
  }
#endif

  /* --------------------------------------------------------------------------
   * Scan through the loaded snapshot list, and build an array of pointers
   * to NUL-terminated file names in rx_frame.snapshot_names.
   * ----------------------------------------------------------------------- */

  while ((*src) && (nbr_snapshots < MAX_SNAPSHOTS)) {
    unsigned char c;

    rx_frame.snapshot_names[nbr_snapshots ++] = src;

    /* Find end of file name */
    while (c = *src) {
      if ((int) c < ' ') {
	      *src = '\0';
        do {
          src ++;
        } while (*src && (*src < ' '));
        break;
      }
      else {
        src ++;
      }
    }
  }

  /* --------------------------------------------------------------------------
   * Display menu
   * ----------------------------------------------------------------------- */

  set_attrs(INK(BLUE) | PAPER(WHITE), 2, 0, 32 * DISPLAY_LINES);

  /* --------------------------------------------------------------------------
   * Run the menu, act on user input
   * ----------------------------------------------------------------------- */
  {
    uint16_t idx            = 0;
    uint16_t last_idx       = 1;    /* force update */

    uint16_t display_offset = 0;
    bool    needs_redraw    = true;

    for (;;) {
      key_t key;

      if (idx != last_idx) {
	      set_attrs(INK(BLUE) | PAPER(WHITE),
		              2 + (last_idx - display_offset), 0, 32);

      	if (nbr_snapshots > DISPLAY_LINES) {
      	  if ((idx < display_offset)
      	      || (idx >= (display_offset + DISPLAY_LINES)))
                {
      	    if (idx < (DISPLAY_LINES >> 1)) {
      	      display_offset = 0;
      	    }
      	    else if (idx > (nbr_snapshots - (DISPLAY_LINES >> 1))) {
      	      display_offset = nbr_snapshots - DISPLAY_LINES;
      	    }
      	    else {
      	      display_offset = (idx - (DISPLAY_LINES >> 1));
      	    }
      	    needs_redraw = true;
      	  }
      	}

      	set_attrs(INK(WHITE) | PAPER(BLUE) | BRIGHT,
      		        2 + (idx - display_offset), 0, 32);

        last_idx = idx;
      }

      if (needs_redraw) {
      	uint8_t i;
      	for (i = 0;
      	     (i < DISPLAY_LINES) && ((i + display_offset) < nbr_snapshots);
      	     i++)
        {
      	  print_at(i + 2, 1, '.', rx_frame.snapshot_names[i + display_offset]);
      	}

      	needs_redraw = false;
      }

      key = wait_key();

      switch(key) {
      case KEY_ENTER:
        init_progress_display();
        eth_init();
        expect_snapshot();
        tftp_read_request(rx_frame.snapshot_names[idx]);
        return;
      case KEY_UP:
        if (idx > 0) {
          idx --;
        }
        break;
      case KEY_DOWN:
        if (idx < (nbr_snapshots - 1)) {
          idx ++;
        }
        break;
      default:
      {
        uint8_t i;
        for (i = 0; i < nbr_snapshots; i++) {
          char ch = rx_frame.snapshot_names[i][0];
          if (ch >= 'a' && ch <= 'z') {
            ch &= 0xDF;     /* upper case */
          }
          if (ch >= (int) key) {
            idx = i;
            break;
          }
        }
      }
      }
    }
  }
}
