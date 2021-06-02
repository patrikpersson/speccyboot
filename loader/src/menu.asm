;;
;; Module menu:
;;
;; Display a menu from the loaded snapshot file, and load selected snapshot.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
;;
;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use,
;; copy, modify, merge, publish, distribute, sublicense, and/or sell
;; copies of the Software, and to permit persons to whom the
;; Software is furnished to do so, subject to the following
;; conditions:
;;
;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.
;;
;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;; OTHER DEALINGS IN THE SOFTWARE.

    .module menu
    .optsdcc -mz80

    .include "menu.inc"

    .include "context_switch.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "ui.inc"
    .include "util.inc"
    .include "z80_loader.inc"

;; ============================================================================

DISPLAY_LINES = 20   ;; number of snapshot names displayed at a time

KEY_ENTER     = 13
KEY_UP        = '7'
KEY_DOWN      = '6'

;; position for loader version (one lower-case letter)

VRAM_LOADER_VERSION = BITMAP_BASE + 14   ;; (0, 14)

;; ============================================================================

    .area _CODE

;; ----------------------------------------------------------------------------
;; print_entry:
;;
;; call with
;; HL = filename (terminated by NUL or '.')
;; DE = VRAM address
;;
;; Returns with HL pointing to '.' or NUL,
;; and DE pointing to first cell on next line
;; Destroys AF, BC
;; ----------------------------------------------------------------------------

print_entry:
    ld   a, e
    and  a, #0x1f
    ret  z
    ld   a, (hl)
    or   a, a
    jr   z, pad_to_end_of_line
    cp   a, #'.'
    jr   z, pad_to_end_of_line
    call print_char
    inc  hl
    jr   print_entry

;; ----------------------------------------------------------------------------
;; pad_to_end_of_line:
;;
;; call with
;; DE = VRAM address
;;
;; Fills the remaining cells on the line with spaces.
;; On exit, DE points to the first cell on the next line.
;; ----------------------------------------------------------------------------

pad_to_end_of_line:

    ld   a, e
    and  a, #0x1f
    ret  z
    ld   a, #' '
    call print_char
    jr   pad_to_end_of_line

;; ----------------------------------------------------------------------------
;; Set up table of file name pointers in _rx_frame.
;; Returns number of snapshots in the list in register E.
;; ----------------------------------------------------------------------------

    .area _CODE

menu_setup:

    ;; ========================================================================
    ;; Scan through the loaded snapshot list, and build an array of pointers
    ;; to NUL-terminated file names in rx_frame.
    ;; ========================================================================

    ld   bc, #_snapshot_list
    ld   hl, #_rx_frame

    ld   e, #0

    ;; ------------------------------------------------------------------------
    ;; check if done:
    ;; - found a NUL byte? (interpreted as end of file)
    ;; - filled RX buffer with filename pointers? (max 255)
    ;; ------------------------------------------------------------------------

menu_setup_loop1:

    ld   a, (bc)
    or   a, a
    ret  z

    ld   a, e
    inc  a
    ret  z

    ;; ------------------------------------------------------------------------
    ;; store a pointer to the current file name
    ;; ------------------------------------------------------------------------

menu_setup_store_ptr:

    ld   (hl), c
    inc  hl
    ld   (hl), b
    inc  hl

    inc  e

    ;; ------------------------------------------------------------------------
    ;; ensure the current file name is NUL terminated, and advance HL to next
    ;; ------------------------------------------------------------------------

menu_setup_loop2:
    ld   a, (bc)
    inc  bc
    cp   a, #' '        ;; less than 32 means end of file name (CR/LF/NUL)
    jr   nc, menu_setup_loop2

    dec  bc
    xor  a, a
    ld   (bc), a

    ;; ------------------------------------------------------------------------
    ;; skip any other trailing CR/LF stuff
    ;; ------------------------------------------------------------------------

menu_setup_find_next:
    inc  bc
    ld   a, (bc)
    cp   a, #' '
    jr   nc, menu_setup_loop1
    or   a, a
    jr   nz, menu_setup_find_next

    ret

;; ============================================================================

    .area _NONRESIDENT

run_menu:

    ;; ========================================================================
    ;; This function will be called twice:
    ;; once to load the snapshot list, and then again once that list is loaded.
    ;; ========================================================================

    ld   de, #TFTP_VRAM_FILENAME_POS
    call pad_to_end_of_line

    ;; ------------------------------------------------------------------------
    ;; on first entry, this branch is patched to jump right upon
    ;; second time
    ;; ------------------------------------------------------------------------

second_time_branch:
    .db  JR_UNCONDITIONAL
second_time_branch_offset:
    .db  0

    ld   a, #menu_second_time - second_time_branch - 2
    ld   (second_time_branch_offset), a

    ;; ========================================================================
    ;; this is the first time the stage 2 loader was invoked:
    ;; load the snapshot list
    ;; ========================================================================

    ld   hl, #snapshots_lst_str
    call tftp_read_request

    jp   main_loop

menu_second_time:

    ;; ========================================================================
    ;; this is the second time the stage 2 loader was invoked:
    ;; run the menu interface
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; attributes for 'S' indicator: black ink, white paper, bright
    ;; ------------------------------------------------------------------------

    ld    hl, #ATTRS_BASE + 23 * 32 + 16           ;; (23, 16)
    ld    (hl), #(BLACK | (WHITE << 3) | BRIGHT)

    ;; ------------------------------------------------------------------------
    ;; set up menu colours
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5840
    ld   bc, #DISPLAY_LINES * 32 - 1
    ld   a, #BLACK + (WHITE << 3) + BRIGHT
    call fill_memory

    ld   a, #VERSION_LOADER + 'a'
    ld   de, #VRAM_LOADER_VERSION
    call print_char

    ;; ========================================================================
    ;; main loop for the menu
    ;;
    ;; C = currently highlighted entry (0..254)
    ;; D = display offset (index of first displayed snapshot name, < E)
    ;; E = total number of snapshots (0..255)
    ;; ========================================================================

    call menu_setup

    ld   c, #0
    ld   d, c

menu_loop:

    ld   a, #WHITE + (BLUE << 3) + BRIGHT
    call menu_set_highlight

    ;; ========================================================================
    ;; redraw menu contents
    ;; ========================================================================

    push bc
    push de

    ld   c, #0     ;; loop index

    exx
    ld   de, #0x4040      ;; (2,0)
    exx

redraw_menu_loop:

    ld   a, c
    cp   a, #DISPLAY_LINES
    jr   nc, redraw_menu_done

    ;; break the loop if C + D >= E

    add  a, d
    cp   a, e
    jr   nc, redraw_menu_done

    exx

    ld   l, a
    ld   h, #0
    add  hl, hl
    ld   bc, #_rx_frame
    add  hl, bc
    ld   a, (hl)
    inc  hl
    ld   h, (hl)
    ld   l, a  ;; HL now points to filename string

    inc  de    ;; skip first cell on each line
    call print_entry

    exx

    inc  c
    jr   redraw_menu_loop

redraw_menu_done:

    ;; ========================================================================
    ;; handle user input
    ;; ========================================================================

    call _wait_key

    pop  de
    pop  bc

    ld   a, #BLACK + (WHITE << 3) + BRIGHT    ;; erase highlight
    call menu_set_highlight

    ld   a, l       ;; return value from _wait_key above

    cp   a, #KEY_ENTER
    jr   z, menu_hit_enter

    cp   a, #KEY_UP
    jr   z, menu_hit_up

    cp   a, #KEY_DOWN
    jr   z, menu_hit_down

    ;; ========================================================================
    ;; user hit something else than ENTER/UP/DOWN:
    ;; select the first snapshot with that initial letter
    ;; ========================================================================

    call find_snapshot_for_key

    jr   menu_adjust

    ;; ========================================================================
    ;; user hit DOWN: highlight next entry
    ;; ========================================================================

menu_hit_down:

    ld   a, c
    inc  a
    cp   a, e
    jr   nc, menu_loop

    inc  c

    jr   menu_adjust

    ;; ========================================================================
    ;; user hit UP: highlight previous entry
    ;; ========================================================================

menu_hit_up:

    ld   a, c
    or   a, a
    jr   z, menu_loop

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

    ;; fall through here: nothing useful will happen,
    ;; but saves a JR menu_loop

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

    ;; ========================================================================
    ;; user hit ENTER: load selected snapshot
    ;; ========================================================================

menu_hit_enter:

    ld   h, #0
    ld   l, c
    add  hl, hl
    ld   de, #_rx_frame
    add  hl, de
    ld   e, (hl)
    inc  hl
    ld   d, (hl)

    push de     ;; push arg for tftp_read_request below

    ;; ------------------------------------------------------------------------
    ;; prepare for receiving .z80 snapshot data
    ;; ------------------------------------------------------------------------

    ld   hl, #z80_loader_receive_hook
    ld   (_tftp_receive_hook), hl

    ld    hl, #0x5800      ;; clear attribute lines 0..22
    ld    bc, #0x2e0
    ld    a, #WHITE + (WHITE << 3)
    call  fill_memory

    ld    c, #0x1f         ;; set attribute line 23 to bright
    ld    a, #WHITE + (WHITE << 3) + BRIGHT
    ld    (hl), a
    ldir

    ld    l, #14
    xor   a, a
    call  show_attr_digit

    ld    l, #25
    ld    de, #_font_data + 8 * (75-32) + 1 ;; address of 'K' bits
    call  show_attr_char_address_known

    ;; ------------------------------------------------------------------------
    ;; send a TFTP request for the snapshot
    ;; ------------------------------------------------------------------------

    call eth_init

    pop  hl
    call tftp_read_request

    ;; ------------------------------------------------------------------------
    ;; let the main loop handle the response
    ;; ------------------------------------------------------------------------

    jp   main_loop

    ;; ========================================================================
    ;; subroutine: select snapshot matching keypress
    ;;
    ;; On entry:
    ;;   E: number of snapshots in list
    ;;   A: pressed key (ASCII)
    ;;
    ;; On exit:
    ;;   C: index of selected snapshot
    ;;
    ;; Destroys AF, B, HL; preserves DE.
    ;; ========================================================================

    .area _CODE

find_snapshot_for_key:

    ld   b, a
    ld   c, #0  ;; result (selected index)

    ld   hl, #_rx_frame
find_snapshot_for_key_lp:

    ld   a, c
    inc  a
    cp   a, e

    ret  nc

    push de

    ld   e, (hl)
    inc  hl
    ld   d, (hl)
    inc  hl
    ld   a, (de)

    pop  de

    cp   a, #'a'
    jr   c, not_lowercase_letter
    and  a, #0xDF     ;; to upper case
not_lowercase_letter:

    cp   a, b
    ret  nc

    inc  c

    jr   find_snapshot_for_key_lp


    ;; ========================================================================
    ;; subroutine: highlight current line to colour in register A
    ;; ========================================================================

    .area _CODE

menu_set_highlight:

    ;; ------------------------------------------------------------------------
    ;; The VRAM attribute address is 0x5840 + 32 * (C - D). This is computed as
    ;; 32 * (C - D + 0x2C2). The difference (C-D) is at most decimal 20, so the
    ;; value (C - D + 0xC2) fits in a byte (at most 0xD6)
    ;; ------------------------------------------------------------------------

    push hl
    push af
    ld   h, #2           ;; high byte of 0x2C2
    ld   a, c
    sub  a, d
    add  a, #0xC2
    ld   l, a   ;; H is now zero
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    pop  af

    ld   b, #32
menu_highlight_loop:
    ld   (hl), a
    inc  hl
    djnz menu_highlight_loop

    pop hl

    ret

    .area _CODE

snapshots_lst_str:
    .ascii "snapshots.lst"
    .db  0
