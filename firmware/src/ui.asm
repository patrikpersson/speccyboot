;;
;; Module ui:
;;
;; access to ZX Spectrum display and keyboard
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

    .module ui
    .optsdcc -mz80

    .include "include/ui.inc"

    .include "include/globals.inc"
    .include "include/util.inc"

;; ============================================================================
;; Repeat time-outs: between the keypress and the first repetition, and for
;; any subsequent repetitions
;;
;; (measured in double-ticks of 20ms)
;; ============================================================================

REPEAT_FIRST_TIMEOUT = 40
REPEAT_NEXT_TIMEOUT  = 10

;; ============================================================================

    .area _DATA

_previous_key:
    .ds   1       ;; initially zero, for no key

_first_repetition:
    .ds   1       ;; flag

;; ============================================================================

    .area _NONRESIDENT

;; ############################################################################
;; _poll_key
;;
;; Return currently pressed key, or KEY_NONE, in register A and B.
;; (The same value is returned in both A and B.)
;;
;; Destroys HL, BC, DE, AF.
;; ############################################################################

_poll_key:

    ld    hl, #key_rows
    ld    bc, #0x7ffe
poll_outer:
    in    d, (c)

    ld    e, #5       ;; number of keys in each row

poll_inner:
    ld    a, (hl)
    inc   hl
    rr    d
    jr    c, not_pressed
    or    a
    jr    nz, poll_done

not_pressed:
    dec   e
    jr    nz, poll_inner

    rrc   b
    jr    c, poll_outer

    xor   a         ;; KEY_NONE == 0

poll_done:
    ld    b, a

    ret

;; ----------------------------------------------------------------------------
;; Keyboard mapping (used by _poll_key above)
;;
;; ZX Spectrum BASIC Programming (Vickers), Chapter 23:
;;
;; IN 65278 reads the half row CAPS SHIFT to V
;; IN 65022 reads the half row A to G
;; IN 64510 reads the half row Q to T
;; IN 63486 reads the half row 1 to 5
;; IN 61438 reads the half row O to 6
;; IN 57342 reads the half row P to 7
;; IN 49150 reads the half row ENTER to H
;; IN 32766 reads the half row SPACE to B
;;
;; http://www.worldofspectrum.org/ZXBasicManual/index.html
;;
;; A '0' in the 'key_rows' table means that key is to be ignored. The rows
;; are ordered for the high byte in the row address to take values in the
;; following order:
;;
;; 01111111
;; 10111111
;; 11011111
;; 11101111
;; 11110111
;; 11111011
;; 11111101
;; 11111110
;; ----------------------------------------------------------------------------

key_rows:
    .db  0x20, 0, 0x4d, 0x4e, 0x42      ;; 7FFE: space, shift, 'M', 'N', 'B'
    .db  0x0d, 0x4c, 0x4b, 0x4a, 0x48   ;; BFFE: enter, 'L', 'K', 'J', 'H'
    .db  0x50, 0x4f, 0x49, 0x55, 0x59   ;; DFFE: 'P', 'O', 'I', 'U', 'Y'
    .db  0x30, 0x39, 0x38, 0x37, 0x36   ;; EFFE: '0', '9', '8', '7', '6'
    .db  0x31, 0x32, 0x33, 0x34, 0x35   ;; F7FE: '1', '2', '3', '4', '5'
    .db  0x51, 0x57, 0x45, 0x52, 0x54   ;; FBDE: 'Q', 'W', 'E', 'R', 'T'
    .db  0x41, 0x53, 0x44, 0x46, 0x47   ;; FDFE: 'A', 'S', 'D', 'F', 'G'
    .db  0, 0x5a, 0x58, 0x43, 0x56      ;; FEFE: shift, 'Z', 'X', 'C', 'V'


;; ############################################################################
;; _wait_key
;; ############################################################################

_wait_key:

    ;; ------------------------------------------------------------------------
    ;; is the previous key still being pressed?
    ;; ------------------------------------------------------------------------

    call _poll_key

    or   a, a
    jr   z, wait_key_no_repetition
    ld   a, (_previous_key)
    cp   a, b
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; yes, the previous key is still being pressed
    ;; see if it remains pressed until the repetition timer expires
    ;; ------------------------------------------------------------------------

wait_key_repetition_loop:

    call _poll_key
    ld   a, (_previous_key)
    cp   a, b
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; decide on a timeout, depending on whether this is the first repetition
    ;; ------------------------------------------------------------------------

    ld   hl, #_timer_tick_count + 1
    ld   a, (hl)     ;; high byte of ticks
    or   a, a        ;; non-zero? then definitely timeout
    jr   nz, wait_key_repeat
    ld   a, (_first_repetition)
    or   a, a
    ld   a, #REPEAT_FIRST_TIMEOUT
    jr   nz, wait_key_check_repetition
    ld   a, #REPEAT_NEXT_TIMEOUT
wait_key_check_repetition:
    dec  hl          ;; now points to low byte of ticks
    cp   a, (hl)
    jr   nc, wait_key_repetition_loop

    ;; ------------------------------------------------------------------------
    ;; we have a repeat event, and this is no longer the first repetition
    ;; ------------------------------------------------------------------------

wait_key_repeat:
    xor  a, a              ;; value for _first_repetition
    jr   wait_key_finish

    ;; ------------------------------------------------------------------------
    ;; no repetition: instead wait for a key to become pressed
    ;; ------------------------------------------------------------------------

wait_key_no_repetition:

    call _poll_key
    or   a, a
    jr   z, wait_key_no_repetition

    ld   (_previous_key), a

    ;; ------------------------------------------------------------------------
    ;; Any repetition after this will be the first one, so A needs to be set
    ;; to a non-zero value. And it is: it is the non-zero key value.
    ;; ------------------------------------------------------------------------

wait_key_finish:
    ;; assume A holds value for _first_repetition, and B holds result
    ld   hl, #0
    ld   (_timer_tick_count), hl
    ld   (_first_repetition), a
    ld   l, b      ;; _poll_key returned same value in A and B
    ret
