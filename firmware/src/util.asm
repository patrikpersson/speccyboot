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
    .optsdcc -mz80

    .include "include/util.inc"

    .include "include/globals.inc"

    .area _DATA

;; ----------------------------------------------------------------------------
;; Tick count, increased by 2 (!) by the 50Hz timer ISR in crt0.asm.
;; Means that the high byte is increased every 2.56 seconds.
;; ----------------------------------------------------------------------------

_timer_tick_count:
    .ds   2


;; ############################################################################
;; _memory_compare
;; ############################################################################

    .area _CODE

_memory_compare:

    ld   a, (de)
    cp   a, (hl)
    ret  nz
    inc  de
    inc  hl
    djnz _memory_compare
    ret


;; ############################################################################
;; _fail
;; ############################################################################

_fail:
    out  (ULA_PORT), a
    di
    halt

;; ############################################################################
;; print_ip_addr
;; ############################################################################

print_ip_addr:

    ;; DE = VRAM pointer
    ;; HL = IP address
    ;; AF, BC = scratch

    ld    b, #0       ;; loop counter AND octet index
00001$:               ;; four octets

    ld    a, (hl)
    inc   hl
    cp    a, #10
    jr    c, 00004$    ;; < 10? print only single digit
    cp    a, #100
    jr    c, 00002$    ;; no hundreds? skip entirely, not even a zero
    ld    c, #0
00003$:   ;; loop to count hundreds
    inc   c
    sub   a, #100
    cp    a, #100
    jr    nc, 00003$

    push  af
    ld    a, c
    call  print_digit      ;; X__
    pop   af

00002$:   ;; hundreds done
    cp    a, #10
    jr    c, 00004$
    ld    c, #0
00005$:   ;; loop to count tens
    inc   c
    sub   a, #10
    cp    a, #10
    jr    nc, 00005$

    push  af
    ld    a, c
    call  print_digit      ;; X__
    pop   af

00004$:   ;; tens done

    call  print_digit      ;; __X

    ;; print period?
    inc   b
    ld    a, b
    cp    a, #4            ;; last octet? no period
    ret   z

    ld    a, #'.'
    call  print_char
    jr    00001$           ;; next octet

;; ############################################################################

print_digit:
    and  a, #0x0f
    add  a, #'0'

    ;; FALL THROUGH to print_char


;; ############################################################################
;; _print_char
;; ############################################################################

print_char:

    push hl
    push bc

    ld   l, a
    ld   h, #0
    add  hl, hl
    add  hl, hl
    add  hl, hl
    ld   bc, #_font_data - 32 * 8
    add  hl, bc

    ld   b, #8
    ld   c, d
_print_char_loop:
    ld   a, (hl)
    ld   (de), a
    inc  d
    inc  hl
    djnz _print_char_loop
    ld   d, c

    inc  e

    pop  bc
    pop  hl

    ret
