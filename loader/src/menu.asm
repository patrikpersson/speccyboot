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
;; Repeat time-outs: between the keypress and the first repetition, and for
;; any subsequent repetitions
;;
;; (measured in double-ticks of 20ms)
;; ============================================================================

REPEAT_FIRST_TIMEOUT = 40
REPEAT_NEXT_TIMEOUT  = 10

;; BASIC ROM1 entry points

rom_key_scan         = 0x028E
rom_keymap           = 0x0205

;; opcode for runtime patching

CP_A_N               = 0xfe

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

;; ############################################################################
;; print_str
;;
;; Prints a string, terminated by '.' (that is, _not_ NUL).
;;
;; The string is truncated to the end of the line, and padded with spaces.
;;
;; HL points to the string to print
;; DE points to VRAM location
;; destroys AF and HL; preserves BC.
;; DE will point to the first character cell on the following line.
;; ############################################################################

    .area _NONRESIDENT

print_str:

    ld   a, (hl)
    cp   a, #'.'
    jr   nz, no_padding
    ld   a, #' '
    .db  JR_NZ          ;; Z is set here, so this will skip the INC HL below

no_padding:

    inc  hl

    or   a, a
    ret  z

    call print_char

    jr   nz, no_end_of_segment

    ;; E became zero: means we reached the end of one of the 2K VRAM segments,
    ;; skip to the next one

    ld   a, d
    add  a, #8
    ld   d, a

no_end_of_segment:

    ld   a, e
    and  a, #0x1f
    jr   nz, print_str

    ret

;; ============================================================================

    .area _STAGE2_ENTRY

run_menu:

    ;; ------------------------------------------------------------------------
    ;; set up menu colours
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5800
    ld   de, #0x5801
    ld   bc, #2*32                                               ;; lines 0..1
    ld   (hl), #BLACK + (WHITE << 3)
    ldir

    ld   bc, #DISPLAY_LINES * 32 - 1                             ;; lines 2..21
    ld   (hl), #BLACK + (WHITE << 3) + BRIGHT
    ldir

    ;; ------------------------------------------------------------------------
    ;; attributes for 'S' indicator: black ink, white paper, bright
    ;; (same as menu background above)
    ;; ------------------------------------------------------------------------

    ;; H already has the right value here

    ld    l, #<ATTRS_BASE + 23 * 32 + 16            ;; (23, 16)
    ld    (hl), #BLACK + (WHITE << 3) + BRIGHT

    ;; ------------------------------------------------------------------------
    ;; print 'SpeccyBoot <version>' at (0,0)
    ;; ------------------------------------------------------------------------

    ld    hl, #title_str                ;; 'SpeccyBoot <version>'
    ld    de, #BITMAP_BASE + 0x0100     ;; coordinates (0,0)

    call  print_str

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

    call wait_for_key

    pop  de
    pop  bc


    ld   a, #BLACK + (WHITE << 3) + BRIGHT
    call menu_set_highlight

    ld   a, l       ;; return value from wait_for_key above

    cp   a, #KEY_ENTER
    jr   z, menu_hit_enter

    sub  a, #KEY_DOWN
    jr   z, menu_hit_down
    dec  a                ;; KEY_UP?
    jr   z, menu_hit_up

    ;; ========================================================================
    ;; user hit something else than ENTER/UP/DOWN:
    ;; select the first snapshot with that initial letter
    ;; ========================================================================

    ld   c, b ;; C will hold the result (selected index); B==0 after menu_erase_highlight
    ld   b, l

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
    ;; Set up snapshot progress display. Present progress bar and digit '0'
    ;; immediately, as instant user feedback, although the progress bar
    ;; will be put in place upon the first TFTP data packet too (in s_header).
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5800      ;; clear attribute lines 0..22
    ld   de, #0x5801
    ld   bc, #0x2E0
    ld   (hl), #WHITE + (WHITE << 3)
    ldir

    ld   c, #0x1f
    ld   (hl), #WHITE + (WHITE << 3) + BRIGHT
    ldir

    xor  a, a
    call show_attr_digit_right

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


    .area _NONRESIDENT

title_str:
    .ascii "SpeccyBoot "
    .db   VERSION_STAGE1 + '0'
    .db   0


    .area _NONRESIDENT

;; ############################################################################
;; subroutine: highlight current line to colour (in register C)
;;
;; destroys B, AF; preserves DE, HL
;; on return B==0 
;; ############################################################################

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
;; wait_for_key
;;
;; wait for keypress. Handles repeat events.
;; ############################################################################

    .area _NONRESIDENT

wait_for_key:

    ;; ------------------------------------------------------------------------
    ;; is any key being pressed?
    ;; ------------------------------------------------------------------------

    call scan_key
    jr   z, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; is the previous key still being pressed?
    ;; ------------------------------------------------------------------------

    ld   a, c

    .db  CP_A_N
previous_key:
    .db  0                        ;; value patched at runtime

    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; yes, the previous key is still being pressed
    ;; see if the same key remains pressed until the repetition timer expires
    ;; ------------------------------------------------------------------------

wait_key_repetition_loop:

    halt                                ;; allow for an interrupt to occur

    call scan_key
    jr   z, wait_key_no_repetition      ;; key released?

    ;; ------------------------------------------------------------------------
    ;; decide on a timeout, depending on whether this is the first repetition
    ;; ------------------------------------------------------------------------

    ld   a, (_timer_tick_count)

    .db  CP_A_N
wait_key_timeout:
    .db  REPEAT_FIRST_TIMEOUT          ;; value patched at runtime

    jr   c, wait_key_repetition_loop

    ;; ------------------------------------------------------------------------
    ;; we have a repeat event, and this is no longer the first repetition
    ;; ------------------------------------------------------------------------

wait_key_repeat:
    ld   a, #REPEAT_NEXT_TIMEOUT
    jr   wait_key_finish

    ;; ------------------------------------------------------------------------
    ;; no repetition: instead wait for a key to become pressed
    ;; ------------------------------------------------------------------------

wait_key_no_repetition:

    call scan_key
    jr   z, wait_key_no_repetition

    ld   a, c
    ld   (previous_key), a
    ld   a, #REPEAT_FIRST_TIMEOUT

wait_key_finish:
    ;; assume A holds value for is_first_repetition, and C holds result
    ld   hl, #0
    ld   (_timer_tick_count), hl
    ld   (wait_key_timeout), a
    ld   l, c
    ret

;; ############################################################################
;; scan_key
;;
;; Return currently pressed key, if any, in register C.
;; Z flag is set if no key is pressed, cleared if any key is set.
;;
;; Must run from RAM: pages in the BASIC ROM for keyboard scanning.
;;
;; Destroys HL, BC, DE, AF.
;; ############################################################################

    .area _NONRESIDENT

scan_key:
    di
    ld    a, #SPI_IDLE+SPI_CS+PAGE_OUT   ;; page out SpeccyBoot
    out   (SPI_OUT), a
    call  rom_key_scan                   ;; destroys AF, BC, DE, HL
    ld    hl, #rom_keymap
    ld    a, e
    add   a, l
    ld    l, a
    ld    c, (hl)
    ld    a, #SPI_IDLE+SPI_CS            ;; page in SpeccyBoot
    out   (SPI_OUT), a
    inc   e                              ;; set Z flag if no key pressed
    ei
    ret
