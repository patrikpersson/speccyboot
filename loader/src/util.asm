;; Module util:
;;
;; Various low-level useful stuff.
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

    .module util

    .include "util.inc"

    .include "globals.inc"

    .area _DATA

;; ----------------------------------------------------------------------------
;; Tick count, increased by 2 (!) by the 50Hz timer ISR in init.asm.
;; Means that the high byte is increased every 2.56 seconds.
;; ----------------------------------------------------------------------------

_timer_tick_count:
    .ds   2

;; ############################################################################
;; fail
;; ############################################################################

    .area _CODE

fail:

  di
  out (ULA_PORT), a
  halt

;; ############################################################################
;; a_div_b
;; ############################################################################

    .area _CODE

a_div_b:

    ld    c, #0
a_div_b_loop:
    cp    a, b
    ret   c
    inc   c
    sub   a, b
    jr    a_div_b_loop

;; ############################################################################
;; fill_memory
;; ############################################################################

    .area _CODE

fill_memory:

    ld    (hl), a
    ld    d, h
    ld    e, l
    inc   de
    ldir
    ret

;; ############################################################################
;; print_str
;; ############################################################################

    .area _CODE

print_str:

    ld   a, h
    cp   a, #>snapshot_array
    ld   a, (hl)
    jr   c, not_a_menu_entry

    ld   a, e
    and  a, #0x1f
    ret  z

    ld   a, (hl)
    cp   a, #'.'
    jr   nz, not_a_menu_entry
    ld   a, #' '
    .db  JR_NZ          ;; Z is set here, so this will skip the INC HL below

not_a_menu_entry:

    inc  hl

    or   a, a
    ret  z

    call print_char
    jr   print_str

