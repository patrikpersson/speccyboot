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
    jr   z, redraw_menu_done   ;; TODO: do this better?

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

static void
handle_menu(void)
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; copy digit font data
    ;; ------------------------------------------------------------------------

    ld   hl, #_font_data + 16 * 8 + 1   ;; first non-zero scanline of "0"
    ld   de, #DIGIT_DATA_ADDR
    ld   a, #10
copy_digit_font_data_loop:
    ld   bc, #6
    ldir
    inc  hl
    inc  hl
    dec  a
    jr   nz, copy_digit_font_data_loop

    ;; ------------------------------------------------------------------------
    ;; ensure menu data is NUL-terminated
    ;; ------------------------------------------------------------------------

    ld   hl, (_tftp_write_pos)
    ld   (hl), a    ;; A is zero after loop above

    ;; ------------------------------------------------------------------------
    ;; set up menu colours
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5840
    ld   de, #0x5841
    ld   bc, #DISPLAY_LINES * 32 - 1
    ld   a, #BLUE + (WHITE << 3)
    ld   (hl), a
    ldir

    ;; ------------------------------------------------------------------------
    ;; create index list, use _rx_frame for filename pointer array
    ;; ------------------------------------------------------------------------

    call _create_snapshot_list
    ld   e, l

    ;; C = currently highlighted entry (0..254)
    ;; D = display offset (index of first displayed snapshot name)
    ;; E = total number of snapshots (0..255)

    ld   c, #0
    ld   d, c

menu_loop:

    ld   a, #WHITE + (BLUE << 3) + BRIGHT
    call menu_set_highlight

    push bc
    push de

    call _redraw_menu

    call _wait_key
    ld   a, l

    pop  de
    pop  bc

    cp   a, #KEY_ENTER
    jr   z, menu_hit_enter

    cp   a, #KEY_UP
    jr   z, menu_hit_up

    cp   a, #KEY_DOWN
    jr   z, menu_hit_down

    push bc
    push de

    ld   b, e
    ld   c, a
    push bc
    call _find_snapshot_for_letter
    pop  af

    pop  de
    pop  bc

    call menu_erase_highlight

    ld   c, l

    jr   menu_adjust

    ;; ------------------------------------------------------------------------
    ;; user hit DOWN: highlight next entry
    ;; ------------------------------------------------------------------------

menu_hit_down:

    ld   a, c
    inc  a
    cp   a, e
    jr   nc, menu_loop

    call menu_erase_highlight

    inc  c

    jr   menu_adjust

    ;; ------------------------------------------------------------------------
    ;; user hit UP: highlight previous entry
    ;; ------------------------------------------------------------------------

menu_hit_up:

    ld   a, c
    or   a, a
    jr   z, menu_loop

    call menu_erase_highlight

    dec  c

    ;; ------------------------------------------------------------------------
    ;; possibly adjust display offset
    ;; ------------------------------------------------------------------------

menu_adjust:

    ;; reached top of display?

    ld   a, c
    cp   a, d
    jr   nc, menu_not_top

    ld   d, c
    jr   menu_loop

menu_not_top:

    ;; reached end of display?

    ld   a, d
    add  a, #DISPLAY_LINES - 1
    cp   a, c
    jr   nc, menu_loop

    ld   a, c
    sub  a, #DISPLAY_LINES - 1
    ld   d, a

    jr   menu_loop

    ;; ------------------------------------------------------------------------
    ;; subroutine: remove highlight for current index
    ;; ------------------------------------------------------------------------

menu_erase_highlight:

    ld   a, #BLUE + (WHITE << 3)

    ;; FALL THROUGH to menu_set_highlight

    ;; ------------------------------------------------------------------------
    ;; subroutine: highlight current index to colour in register A
    ;; ------------------------------------------------------------------------

menu_set_highlight:

    push hl
    push bc
    push af
    ld   h, #0
    ld   a, c
    sub  a, d
    ld   l, a   ;; H is now zero
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    ld   bc, #0x5840      ;; (2,0)
    add  hl, bc
    pop  af
    pop  bc

    ld   b, #32
menu_highlight_loop:
    ld   (hl), a
    inc  hl
    djnz menu_highlight_loop

    pop hl

    ret

    ;; ------------------------------------------------------------------------
    ;; user hit ENTER: load selected snapshot
    ;; ------------------------------------------------------------------------

menu_hit_enter:

    ld   h, #0
    ld   l, c
    add  hl, hl
    ld   de, #_rx_frame
    add  hl, de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)

    push de

    call _eth_init
    call _expect_snapshot
    call _tftp_read_request

    jp   main_loop

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
run_menu(void)
{
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

  // *((uint8_t *) tftp_write_pos) = 0;  // ensure menu data is NUL-terminated

  // copy_digit_font_data();

  /* --------------------------------------------------------------------------
   * Scan through the loaded snapshot list, and build an array of pointers
   * to NUL-terminated file names in rx_frame.snapshot_names.
   * ----------------------------------------------------------------------- */

  /* --------------------------------------------------------------------------
   * Display menu
   * ----------------------------------------------------------------------- */
   // set_attrs(INK(BLUE) | PAPER(WHITE), 2, 0, 32 * DISPLAY_LINES);

  handle_menu();
}
