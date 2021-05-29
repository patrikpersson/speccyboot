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

    .include "include/menu.inc"

    .include "include/context_switch.inc"
    .include "include/eth.inc"
    .include "include/globals.inc"
    .include "include/tftp.inc"
    .include "include/udp_ip.inc"
    .include "include/ui.inc"
    .include "include/util.inc"
    .include "include/z80_loader.inc"

;; ============================================================================

DISPLAY_LINES = 20   ;; number of snapshot names displayed at a time

KEY_ENTER     = 13
KEY_UP        = '7'
KEY_DOWN      = '6'

;; ============================================================================

    .area _CODE

;; ----------------------------------------------------------------------------
;; print_entry:
;;
;; call with
;; IX = filename (terminated by NUL or '.')
;; DE = VRAM address
;;
;; Returns with IX pointing to '.' or NUL,
;; and DE pointing to first cell on next line
;; Destroys AF, BC
;; ----------------------------------------------------------------------------

print_entry:
    ld   a, e
    and  a, #0x1f
    ret  z
    ld   a, (ix)
    or   a, a
    jr   z, pad_to_end_of_line
    cp   a, #'.'
    jr   z, pad_to_end_of_line
    call print_char
    inc  ix
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

    ld   b, #8
    ld   c, d
    xor  a, a
write_space_loop:
    ld   (de), a
    inc  d
    djnz write_space_loop
    ld   d, c
    inc  e
    jr   z, next_2k_segment
    ld   a, e
    and  a, #0x1f
    ret  z
    jr   pad_to_end_of_line

next_2k_segment:
    ld   a, d
    add  a, #8
    ld   d, a
    ret

    .area _CODE

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

    ;; ------------------------------------------------------------------------
    ;; Initialize user interface
    ;; ------------------------------------------------------------------------

    ;; ------------------------------------------------------------------------
    ;; attributes for 'S' indicator: black ink, white paper, bright
    ;; ------------------------------------------------------------------------

    ld    hl, #ATTRS_BASE + 23 * 32 + 16           ;; (23, 16)
    ld    (hl), #(BLACK | (WHITE << 3) | BRIGHT)

    ;; ========================================================================
    ;; this is the first time the stage 2 loader was invoked:
    ;; load the snapshot list
    ;; ========================================================================

    ld   hl, #snapshots_lst_str
    call tftp_read_request

    jp   main_loop

menu_second_time:

    ld   hl, (_tftp_write_pos)
    ld   (hl), #0                   ;; ensure menu data is NUL-terminated

    ;; ========================================================================
    ;; this is the second time the stage 2 loader was invoked:
    ;; run the menu interface
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; set up menu colours
    ;; ------------------------------------------------------------------------

    ld   hl, #0x5840
    ld   bc, #DISPLAY_LINES * 32 - 1
    ld   a, #BLACK + (WHITE << 3) + BRIGHT
    call fill_memory

    ;; ========================================================================
    ;; Scan through the loaded snapshot list, and build an array of pointers
    ;; to NUL-terminated file names in rx_frame.
    ;; The resulting number of snapshots in the list is stored in C.
    ;; ========================================================================

    ld   hl, #_snapshot_list
    ld   de, #_rx_frame

    ;; register C is zero here after fill_memory above

    ;; ------------------------------------------------------------------------
    ;; check if done:
    ;; - found a NUL byte? (interpreted as end of file)
    ;; - filled RX buffer with filename pointers? (max 255)
    ;; ------------------------------------------------------------------------

menu_setup_loop1:

    ld   a, (hl)
    or   a, a
    jr   z, menu_setup_ready

    ld   a, c
    inc  a
    jr   z, menu_setup_ready

    ;; ------------------------------------------------------------------------
    ;; store a pointer to the current file name
    ;; ------------------------------------------------------------------------

menu_setup_store_ptr:

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

menu_setup_loop2:
    ld   a, (hl)
    cp   a, #' '        ;; less than 32 means end of file name (CR/LF/NUL)
    jr   c, menu_setup_found_nul
    inc  hl
    jr   menu_setup_loop2

menu_setup_found_nul:
    xor  a, a
    ld   (hl), a

    ;; ------------------------------------------------------------------------
    ;; skip any other trailing CR/LF stuff
    ;; ------------------------------------------------------------------------

menu_setup_find_next:
    inc  hl
    ld   a, (hl)
    or   a, a
    jr   z, menu_setup_ready
    cp   a, #' '
    jr   nc, menu_setup_loop1
    jr   menu_setup_find_next

menu_setup_ready:

    ;; ========================================================================
    ;; main loop for the menu
    ;;
    ;; C = currently highlighted entry (0..254)
    ;; D = display offset (index of first displayed snapshot name, < E)
    ;; E = total number of snapshots (0..255)
    ;; ========================================================================

    ld   e, c
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
    ld   l, a
    push hl
    pop  ix   ;; IX now points to filename string

    inc  de   ;; skip first cell on each line
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

    push de     ;; DE used for temporary data

    ld   b, e   ;; loop counter
    ld   c, a   ;; pressed key
    ld   e, #0  ;; result (selected index)

    ;; Only search through max-1 entries, and default to the last one if
    ;; nothing is found. This also handles empty lists.

    dec  b
    jr   c, find_snapshot_for_letter_found

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

find_snapshot_for_letter_found:
    ld   c, e

    pop  de        ;; restore E=number of snapshots, D=offset

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
    jr   menu_loop_alternative

menu_not_top:

    ;; reached end of display?

    ld   a, d
    add  a, #DISPLAY_LINES - 1
    cp   a, c
    jr   nc, menu_loop_alternative

    ld   a, c
    sub  a, #DISPLAY_LINES - 1
    ld   d, a

menu_loop_alternative:     ;; TODO: check this when code is tighter
    jp   menu_loop

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
    xor   a
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
    ;; subroutine: highlight current line to colour in register A
    ;; ========================================================================

    .area _CODE

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

    .area _CODE

snapshots_lst_str:
    .ascii "snapshots.lst"
    .db  0
