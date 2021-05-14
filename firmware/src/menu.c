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

#pragma codeseg NONRESIDENT

/* ------------------------------------------------------------------------- */

/* Number of snapshot names displayed at a time */
#define DISPLAY_LINES     (20)

/* -------------------------------------------------------------------------
 * Called to indicate that a .z80 snapshot is expected.
 * Installs a TFTP read hook to relay received data the Z80 snapshot parser.
 * ------------------------------------------------------------------------- */

static void
expect_snapshot(void)
__naked
{
  __asm

    ld   hl, #0x4000
    ld   (_tftp_write_pos), hl

    ld   hl, #_z80_loader_receive_hook
    ld   (_tftp_receive_hook), hl

    ld   hl, #_s_header
    ld   (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

static void
copy_digit_font_data(void)
__naked
{
  __asm

    ld   hl, #_font_data + 16 * 8 + 1   ;; first non-zero scanline of "0"
    ld   de, #DIGIT_DATA_ADDR
    ld   a, #10
copy_digit_font_data_loop::
    ld   bc, #6
    ldir
    inc  hl
    inc  hl
    dec  a
    jr   nz, copy_digit_font_data_loop
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Scan through the loaded snapshot list, and build an array of pointers
 * to NUL-terminated file names in rx_frame.
 * Returns the number of snapshots in the list in L.
 * Destroys AF, C, DE, HL.
 * ----------------------------------------------------------------------- */

static uint8_t
create_snapshot_list(void)
__naked
{
  __asm

    ld   hl, #_snapshot_list
    ld   de, #_rx_frame
    ld   c, #0            ;; number of snapshots, max 255

    ;; ------------------------------------------------------------------------
    ;; check if done:
    ;; - found a NUL byte? (interpreted as end of file)
    ;; - filled RX buffer with filename pointers? (max 255)
    ;; ------------------------------------------------------------------------

create_snapshot_list_loop1:

    ld   a, (hl)
    or   a, a
    jr   z, create_snapshot_list_finish

    ld   a, c
    inc  a
    jr   z, create_snapshot_list_finish

    ;; ------------------------------------------------------------------------
    ;; store a pointer to the current file name
    ;; ------------------------------------------------------------------------

create_snapshot_list_store_ptr:

    ld   a, l
    ld   (de), a
    inc  de
    ld   a, h
    ld   (de), a
    inc  de

    inc  c

    ;; ------------------------------------------------------------------------
    ;; ensure the current file name is NUL terminated, and advance HL to next
    ;; ------------------------------------------------------------------------

create_snapshot_list_loop2:
    ld   a, (hl)
    cp   a, #32        ;; end of file name (CR/LF/NUL)
    jr   c, create_snapshot_list_found_nul
    inc  hl
    jr   create_snapshot_list_loop2

create_snapshot_list_found_nul:
    xor  a, a
    ld   (hl), a

    ;; ------------------------------------------------------------------------
    ;; skip any other trailing CR/LF stuff
    ;; ------------------------------------------------------------------------

create_snapshot_list_find_next:
    inc  hl
    ld   a, (hl)
    or   a, a
    jr   z, create_snapshot_list_finish
    cp   a, #32
    jr   nc, create_snapshot_list_loop1
    jr   create_snapshot_list_find_next

create_snapshot_list_finish:

    ld   l, c

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */
// E = total number of snapshots
// D = display offset (index of first displayed snapshot name)

static void
redraw_menu(uint8_t total, uint8_t offset)
__naked
{
  (void) total, offset;

  __asm

    pop  hl   ;; return adress
    pop  de
    push de
    push hl

    ld   c, #0

redraw_menu_loop:

    ld   a, c
    cp   a, #DISPLAY_LINES
    jr   nc, redraw_menu_done

    ;; C + D < E => C < (E-D) => (E-D) > C
    ld   a, e
    sub  a, d
    cp   a, c
    jr   c, redraw_menu_done

    push bc
    push de

    ld   a, c
    add  a, d
    ld   l, a
    ld   h, #0
    add  hl, hl
    ld   de, #_rx_frame
    add  hl, de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)

    push de       ;; stack string

    ld   a, #'.'
    push af
    inc  sp       ;; stack terminator

    inc  c
    inc  c
    ld   b, #1
    push bc

    call _print_at

    pop  bc
    inc  sp
    pop  hl

    pop  de
    pop  bc

    inc  c
    jr   redraw_menu_loop

redraw_menu_done:
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

static uint8_t
find_snapshot_for_letter(char c, uint8_t nbr_snapshots)
__naked
{
  (void) c, nbr_snapshots;
  __asm

    pop  hl
    pop  bc   ;; C=pressed key, B=nbr_snapshots
    push bc
    push hl

    ld   e, #0  ;; result

    ld   a, b
    or   a, a
    jr   z, find_snapshot_for_letter_found

    ld   hl, #_rx_frame
find_snapshot_for_letter_lp:
    push de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    inc  hl
    ld   a, (de)
    pop  de
    cp   a, #'a'
    jr   c, find_snapshot_for_letter_no_lcase
    cp   a, #'z' + 1
    jr   nc, find_snapshot_for_letter_no_lcase
    and  a, #0xDF     ;; upper case
find_snapshot_for_letter_no_lcase:
    cp   a, c
    jr   nc, find_snapshot_for_letter_found

    inc  e
    djnz find_snapshot_for_letter_lp
    dec  e   ;; choose the last snapshot if none found

find_snapshot_for_letter_found:
    ld   l, e
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
run_menu(void)
{
  unsigned char *src = &snapshot_list;

#ifdef STAGE2_IN_RAM
  if (tftp_write_pos == &snapshot_list) {
#endif
    set_attrs(INK(BLACK) | PAPER(BLACK), 0, 16, 10);
    print_at(23, 0, '\0', "Local:           TFTP:");
    print_ip_addr( &ip_config.host_address, (uint8_t *) LOCAL_IP_POS);
    print_ip_addr( &ip_config.tftp_server_address, (uint8_t *) SERVER_IP_POS);
    set_attrs(INK(WHITE) | PAPER(BLACK), 23, 0, 31);

#ifdef STAGE2_IN_RAM
    tftp_read_request("snapshots.lst");
    __asm
      jp   main_loop
    __endasm;
  }
#endif

  *((uint8_t *) tftp_write_pos) = 0;  // ensure menu data is NUL-terminated

  copy_digit_font_data();

  /* --------------------------------------------------------------------------
   * Scan through the loaded snapshot list, and build an array of pointers
   * to NUL-terminated file names in rx_frame.snapshot_names.
   * ----------------------------------------------------------------------- */

  uint8_t nbr_snapshots = create_snapshot_list();

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

    uint8_t display_offset = 0;
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
        redraw_menu(nbr_snapshots, display_offset);

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
       idx = find_snapshot_for_letter(key, nbr_snapshots);
      }
    }
  }
}
