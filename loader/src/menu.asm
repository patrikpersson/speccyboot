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

    .include "menu.inc"

    .include "context_switch.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"
    .include "z80_loader.inc"

;; ============================================================================

KEY_ENTER     = 13
KEY_UP        = '7'
KEY_DOWN      = '6'

;; ============================================================================

;; ############################################################################
;; subroutine: get filename pointer for index in C (0..255), return
;; pointer in HL. Destroys AF, preserves BC and DE.
;; ############################################################################

    .area _NONRESIDENT

get_filename_pointer:
    push  bc
    ld   h, #0
    ld   l, c
    add  hl, hl
    ld   bc, #snapshot_array
    add  hl, bc
    ld   a, (hl)
    inc  hl
    ld   h, (hl)
    ld   l, a
    pop   bc
    ret

;; ============================================================================

    .area _STAGE2_ENTRY

run_menu:

    ;; ========================================================================
    ;; main loop for the menu
    ;;
    ;; C = currently highlighted entry (0..254)
    ;; D = display offset (index of first displayed snapshot name, < E)
    ;; E = total number of snapshots (0..255)
    ;; ========================================================================

    ld   c, #0
    ld   d, c
    ld   a, (nbr_snapshots)
    ld   e, a

menu_loop:

    ld   a, #BLACK + (GREEN << 3) + BRIGHT
    call menu_set_highlight

    ;; ========================================================================
    ;; redraw menu contents
    ;; ========================================================================

    push bc
    push de

    ;; Set up B to be ((last index to display) + 1)

    ld   c, d     ;; C=first index
    ld   a, c
    add  a, #DISPLAY_LINES
    cp   a, e
    ld   b, a
    jr   c, redraw_menu_limit_set
    ld   b, e
redraw_menu_limit_set:

    ld   de, #0x4141      ;; (2,1)

redraw_menu_loop:

    call get_filename_pointer
    call print_str

    inc  de    ;; skip first cell on each line
    inc  c

    ld   a, c
    cp   a, b
    jr   c, redraw_menu_loop

    ;; ========================================================================
    ;; handle user input
    ;; ========================================================================

    call wait_key

    pop  de
    pop  bc


    ld   a, #BLACK + (WHITE << 3) + BRIGHT
    call menu_set_highlight

    ld   a, (hl)                                  ;; result from wait_key above

    cp   a, #KEY_ENTER
    jr   z, menu_hit_enter

    sub  a, #KEY_DOWN
    jr   z, menu_hit_down
    dec  a                                                           ;; KEY_UP?
    jr   z, menu_hit_up

    ;; ========================================================================
    ;; user hit something else than ENTER/UP/DOWN:
    ;; select the first snapshot with that initial letter
    ;; ========================================================================

    ld   c, b ;; C will hold the result (selected index); B==0 after menu_erase_highlight
    ld   b, (hl)

find_snapshot_for_key_lp:

    ld   a, c
    inc  a
    cp   a, e          ;; ensure (C + 1) < E
    jr   nc, menu_adjust

    call get_filename_pointer

    ld   a, (hl)

    cp   a, #'a'
    jr   c, not_lowercase_letter
    and  a, #0xDF     ;; to upper case
not_lowercase_letter:

    cp   a, b
    jr   nc, menu_adjust

    inc  c

    jr   find_snapshot_for_key_lp

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

menu_adjust:

    ;; ------------------------------------------------------------------------
    ;; adjust D (display offset) to ensure
    ;;   D <= C < E
    ;; and
    ;;   D <= C < D + DISPLAY_LINES
    ;; ------------------------------------------------------------------------

    ;; C < D? Reached top of display?

    ld   a, c
    cp   a, d
    jr   nc, ensure_visible_not_top

    ;; C < D: adjust D to ensure index C is visible

    ld   d, c

ensure_visible_not_top:

    ;; reached end of display?

    ld   a, c
    sub  a, #DISPLAY_LINES - 1
    jr   c, menu_loop
    cp   a, d
    jr   c, menu_loop

    ld   d, a

    jr   menu_loop

    ;; ========================================================================
    ;; user hit ENTER: load selected snapshot
    ;; ========================================================================

menu_hit_enter:

    call get_filename_pointer

    push hl     ;; push arg for tftp_read_request below

    ;; ------------------------------------------------------------------------
    ;; send a TFTP request for the snapshot
    ;; ------------------------------------------------------------------------

    call eth_init

    pop  de
    call tftp_request_snapshot

    ;; ------------------------------------------------------------------------
    ;; let the main loop handle the response
    ;; ------------------------------------------------------------------------

    jp   main_loop

;; ############################################################################
;; subroutine: highlight current line to colour (in register C)
;;
;; destroys B, AF; preserves DE, HL
;; on return B==0 
;; ############################################################################

    .area _CODE

menu_set_highlight:

    ;; ------------------------------------------------------------------------
    ;; The VRAM attribute address is 0x5840 + 32 * (C - D). This is computed as
    ;; 32 * (C - D + 0x2C2). The difference (C-D) is at most decimal 20, so the
    ;; value (C - D + 0xC2) fits in a byte (at most 0xD6)
    ;; ------------------------------------------------------------------------

    push hl
    push af
    ld   h, #>0x2C2
    ld   a, c
    sub  a, d
    add  a, #<0x2C2
    ld   l, a
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

;; ############################################################################
;; wait_key
;;
;; Blocks until a key is pressed. If any key is pressed when the routine is
;; called, block until that key is released.
;;
;; Destroys AF, BC, D, HL.
;;
;; On return, HL points to the pressed key (ASCII).
;; ############################################################################

    .area _NONRESIDENT

wait_key:
    call scan_key
    jr   nc, wait_key

wait_for_key_pressed:

    call scan_key
    jr   c, wait_for_key_pressed

    ret

;; --------------------------------------------------------------------------
;; Subroutine: scan keyboard
;;
;; If a key is pressed, carry is cleared and HL points to the key (ASCII).
;; If no key is pressed, carry is set.
;; ---------------------------------------------------------------------------

scan_key:

    ;; -----------------------------------------------------------------------
    ;; These loops may seem a bit backwards, but they're written to work with
    ;; the keymap copied from ROM.
    ;; -----------------------------------------------------------------------

    ld    hl, #keymap
    ld    d, #0x10                                         ;; start with bit 4

scan_key_row_loop:

    ld    bc, #0x7ffe

scan_key_col_loop:

    ;; -----------------------------------------------------------------------
    ;; Ignore caps shift (whose bogus key value happens to have bit 7 set).
    ;; Arrow keys can otherwise be erroneously be detected as CAPS pressed.
    ;;
    ;; This sets carry flag, indicating no (real) key being pressed.
    ;; -----------------------------------------------------------------------

    ld    a, (hl)
    rla
    ret   c

    in    a, (c)
    and   a, d                                            ;; clears carry flag
    ret   z

    inc   hl
    rrc   b
    jr    c, scan_key_col_loop
    rr    d
    jr    nc, scan_key_row_loop

    ;; carry is set here after JR NC above

    ret
