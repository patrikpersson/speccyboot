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
    .include "include/spi.inc"
    .include "include/util.inc"

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

;; ============================================================================

    .area _DATA

_previous_key:
    .ds   1       ;; initially zero, for no key

is_first_repetition:
    .ds   1       ;; flag

;; ============================================================================

    .area _NONRESIDENT

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


;; ############################################################################
;; _wait_key
;; ############################################################################

_wait_key:

    ;; ------------------------------------------------------------------------
    ;; is the previous key still being pressed?
    ;; ------------------------------------------------------------------------

    call scan_key
    jr   z, wait_key_no_repetition
    ld   a, (_previous_key)
    cp   a, c
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; yes, the previous key is still being pressed
    ;; see if the same key remains pressed until the repetition timer expires
    ;; ------------------------------------------------------------------------

wait_key_repetition_loop:

    halt                                ;; allow for an interrupt to occur

    call scan_key
    jr   z, wait_key_no_repetition      ;; key released?

    ld   a, (_previous_key)
    cp   a, c
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; decide on a timeout, depending on whether this is the first repetition
    ;; ------------------------------------------------------------------------

    ld   a, (is_first_repetition)
    or   a, a
    ld   b, #REPEAT_FIRST_TIMEOUT
    jr   nz, wait_key_check_repetition
    ld   b, #REPEAT_NEXT_TIMEOUT
wait_key_check_repetition:
    ld   a, (_timer_tick_count)
    cp   a, b
    jr   c, wait_key_repetition_loop

    ;; ------------------------------------------------------------------------
    ;; we have a repeat event, and this is no longer the first repetition
    ;; ------------------------------------------------------------------------

wait_key_repeat:
    xor  a, a              ;; value for is_first_repetition
    jr   wait_key_finish

    ;; ------------------------------------------------------------------------
    ;; no repetition: instead wait for a key to become pressed
    ;; ------------------------------------------------------------------------

wait_key_no_repetition:

    call scan_key
    jr   z, wait_key_no_repetition

    ld   a, c
    ld   (_previous_key), a

    ;; ------------------------------------------------------------------------
    ;; Any repetition after this will be the first one, so A needs to be set
    ;; to a non-zero value. And it is: it is the non-zero key value.
    ;; ------------------------------------------------------------------------

wait_key_finish:
    ;; assume A holds value for is_first_repetition, and C holds result
    ld   hl, #0
    ld   (_timer_tick_count), hl
    ld   (is_first_repetition), a
    ld   l, c
    ret
