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

    .area _CODE

get_filename_pointer:
    ld   h, #0x32                   ;; high byte of 0x6400; must match .lk file
    ld   l, c
    add  hl, hl
    inc  hl
    ld   a, (hl)
    inc  hl
    ld   h, (hl)
    ld   l, a
    ret

;; ############################################################################
;; tftp_state_menu_loader
;; ############################################################################

tftp_state_menu_loader:

    ld  hl, #rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    bit 1, b   ;; see below
    ldir

    ;; ------------------------------------------------------------------------
    ;; If a full TFTP packet was loaded, more data will follow, so return.
    ;; (BC above should be exactly 0x200 for all DATA packets except the last
    ;; one, never larger; so we are done for now if bit 1 was set in B.)
    ;; ------------------------------------------------------------------------

    ret nz

    ;; ========================================================================
    ;; main loop for the menu
    ;;
    ;; C = currently highlighted entry (0..254)
    ;; D = display offset (index of first displayed snapshot name, < E)
    ;; E = total number of snapshots (0..255)
    ;; ========================================================================

    ;; BC == 0 here, from LDIR above

    ld   a, (nbr_snapshots)
    ld   e, a

    ;; -----------------------------------------------------------------------
    ;; align the display offset with the currently highlighted entry
    ;; -----------------------------------------------------------------------

align_top:

    ld   d, c

menu_loop:

    ld   a, #BLACK + (GREEN << 3) + BRIGHT
    call menu_set_highlight

    ;; ========================================================================
    ;; redraw menu contents
    ;; ========================================================================

    push de
    push bc

    ;; -----------------------------------------------------------------------
    ;; register allocation while redrawing:
    ;;
    ;; B:  loop counter, number of entries to show (range 1..DISPLAY_LINES)
    ;; C:  current entry to print
    ;; DE: VRAM address
    ;; HL: filename pointer
    ;; -----------------------------------------------------------------------

    ld   c, d

    ld   a, c
    add  a, #DISPLAY_LINES
    cp   a, e
    jr   c, redraw_menu_limit_set
    ld   a, e
redraw_menu_limit_set:
    sub  a, c
    ld   b, a

    ld   de, #0x4101      ;; (0,1)

redraw_menu_loop:

    call get_filename_pointer

    ;; ------------------------------------------------------------------------
    ;; print string, terminated by '.' (that is, _not_ NUL).
    ;;
    ;; The string is truncated to the end of the line, and padded with spaces.
    ;; ------------------------------------------------------------------------

print_string_loop:

    ld   a, (hl)
    cp   a, #'.'
    jr   nz, no_padding
    ld   a, #' '
    .db  JR_NZ          ;; Z is set here, so this will skip the INC HL below

no_padding:

    inc  hl

    call print_char

    ld   a, e
    and  a, #0x1f
    jr   nz, print_string_loop

    inc  de    ;; skip first cell on each line
    inc  c

    djnz redraw_menu_loop

    ;; ========================================================================
    ;; handle user input
    ;; ========================================================================

    call wait_key

    pop  bc
    pop  de

    ld   a, #BLACK + (WHITE << 3) + BRIGHT
    call menu_set_highlight

    ld   a, (hl)                                  ;; result from wait_key above

    cp   a, #KEY_ENTER
    jr   z, menu_hit_enter

    ;; ------------------------------------------------------------------------
    ;; any key less than KEY_UP is treated as KEY_DOWN
    ;; ------------------------------------------------------------------------

    cp   a, #KEY_UP
    ld   a, c
    jr   z, menu_hit_up
    jr   nc, menu_search

    ;; ========================================================================
    ;; user hit DOWN: highlight next entry
    ;; ========================================================================

    inc  a
    cp   a, e
    jr   nc, menu_loop

    inc  c

    jr   menu_adjust

menu_search:

    ;; ========================================================================
    ;; user hit something else than ENTER/UP/DOWN:
    ;; select the first snapshot with that initial letter
    ;;
    ;; search the filename array, and set C to the chosen index
    ;; ========================================================================

    ld   c, b                                  ;; B==0 after menu_set_highlight
    ld   b, (hl)

find_snapshot_for_key:

    call get_filename_pointer

    ld   a, (hl)

    ;; ------------------------------------------------------------------------
    ;; skip check for upper/lower case here:
    ;;
    ;; digit keys or space aren't expected to work here anyway
    ;; (as digits < KEY_DOWN are considered equivalent to KEY_DOWN above)
    ;; ------------------------------------------------------------------------

    inc  c

    and  a, #0xDF     ;; to upper case
    cp   a, b
    jr   nc, dec_c_and_adjust_menu

    ld   a, c
    cp   a, e                                             ;; ensure (C + 1) < E
    jr   c, find_snapshot_for_key

    ;; FALL THROUGH to case menu_hit_up: A != 0, and DEC C works fine here


    ;; ========================================================================
    ;; user hit UP: highlight previous entry
    ;; ========================================================================

menu_hit_up:

    or   a, a
    jr   z, menu_loop

dec_c_and_adjust_menu:

    dec  c

menu_adjust:

    ;; ------------------------------------------------------------------------
    ;; C < D? Reached top of display? The set display offset D := C
    ;; ------------------------------------------------------------------------

    ld   a, c
    sub  a, d
    jr   c, align_top

    ;; ------------------------------------------------------------------------
    ;; (C-D) >= DISPLAY_LINES ? Reached end of display?
    ;; ------------------------------------------------------------------------

    sub  a, #DISPLAY_LINES - 1
    jr   c, menu_loop                ;; no, keep D as is

    ;; ------------------------------------------------------------------------
    ;; reached end of display
    ;;
    ;; at this point, A == C - D - (DISPLAY_LINES-1)
    ;;
    ;; set D := A + D == C - (DISPLAY_LINES-1),
    ;; so C becomes the last visible entry
    ;; ------------------------------------------------------------------------

    add  a, d
    ld   d, a

    jr   menu_loop

    ;; ========================================================================
    ;; user hit ENTER: load selected snapshot
    ;; ========================================================================

menu_hit_enter:

    call get_filename_pointer

    push hl                         ;; push arg for tftp_request_snapshot below

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
    ;; The VRAM attribute address is 0x5800 + 32 * (C - D). This is computed as
    ;; 32 * (C - D + 0x2C0). The difference (C-D) is at most decimal 20, so the
    ;; value (C - D + 0xC0) fits in a byte (at most 0xD4)
    ;; ------------------------------------------------------------------------

    push hl
    ex   af, af'
    ld   h, #>0x2C0
    ld   a, c
    sub  a, d
    add  a, #<0x2C0
    ld   l, a
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, hl

  ;; ========================================================================
  ;; ARP Ethertype (0x08 0x06)
  ;; ========================================================================

ethertype_arp:

    ex   af, af'
    ld   b, #32

menu_highlight_loop:
    ld   (hl), a
    inc  hl
    djnz menu_highlight_loop

    pop hl

    ret

;; ###########################################################################
;; wait_key
;;
;; Blocks until a key is pressed. If any key is pressed when the routine is
;; called, block until that key is released.
;;
;; Destroys AF, BC, DE, HL.
;;
;; On return, HL points to the pressed key (ASCII).
;; ###########################################################################

    .area _CODE

wait_key:

    ld    bc, #ULA_PORT                 ;; set B := 0, to detect any key press

wait_until_no_key_pressed:

    in    a, (c)
    cpl
    and   a, #0x1f
    jr    nz, wait_until_no_key_pressed

    ;; =======================================================================
    ;; Scan keyboard
    ;; =======================================================================

scan_key:

    ;; -----------------------------------------------------------------------
    ;; These loops may seem a bit backwards, but they're written to work with
    ;; the keymap copied from ROM.
    ;; -----------------------------------------------------------------------

    ld    hl, #keymap
    ld    d, #0x10                                         ;; start with bit 4

scan_key_row_loop:

    ld    b, #0x7f                             ;; first key row, C == ULA_PORT

scan_key_col_loop:

    ;; -----------------------------------------------------------------------
    ;; Ignore caps shift (whose bogus key value happens to have bit 7 set).
    ;; Arrow keys can otherwise be erroneously be detected as CAPS pressed.
    ;;
    ;; This also terminates the outer loop, since CAPS is the last entry
    ;; in the keymap table.
    ;; -----------------------------------------------------------------------

    bit   7, (hl)
    jr    nz, scan_key

    in    a, (c)
    and   a, d
    ret   z

    inc   hl
    rrc   b
    jr    c, scan_key_col_loop
    rr    d
    jr    scan_key_row_loop
