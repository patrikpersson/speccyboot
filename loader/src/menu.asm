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
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
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

    ld   a, e
    or   a, a
    ld   a, #FATAL_NO_SNAPSHOTS
    jp   z, fail

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

    call get_filename_pointer

    inc  de    ;; skip first cell on each line
    call print_entry

    exx

    inc  c
    jr   redraw_menu_loop

redraw_menu_done:

    ;; ========================================================================
    ;; handle user input
    ;; ========================================================================

    call wait_for_key

    pop  de
    pop  bc

    ld   a, #BLACK + (WHITE << 3) + BRIGHT    ;; erase highlight
    call menu_set_highlight

    ld   a, l       ;; return value from wait_for_key above

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

    ld   a, c
    call get_filename_pointer

    push hl     ;; push arg for tftp_read_request below

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
    ;; subroutine: get filename pointer for index in A (0..255), return
    ;; pointer in HL
    ;; ========================================================================

    .area _CODE

get_filename_pointer:
    push  bc
    ld   h, #0
    ld   l, a
    add  hl, hl
    ld   bc, #_rx_frame
    add  hl, bc
    ld   a, (hl)
    inc  hl
    ld   h, (hl)
    ld   l, a
    pop   bc
    ret

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

;; ============================================================================

    .area _NONRESIDENT


;; ############################################################################
;; wait_for_key
;;
;; wait for keypress. Handles repeat events.
;; ############################################################################

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

    ld   a, (previous_key)
    cp   a, c
    jr   nz, wait_key_no_repetition

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