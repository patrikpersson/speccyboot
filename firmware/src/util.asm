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
;; Tick count, increased by 2 (!) by the 50Hz timer ISR in init.asm.
;; Means that the high byte is increased every 2.56 seconds.
;; ----------------------------------------------------------------------------

_timer_tick_count:
    .ds   2


    .area _CODE


;; ############################################################################
;; fail
;; ############################################################################

fail:

  di
  out (ULA_PORT), a
  halt

;; ############################################################################
;; print_ip_addr
;; ############################################################################

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

fill_memory:
    ld    (hl), a
    ld    d, h
    ld    e, l
    inc   de
    ldir
    ret

;; ############################################################################
;; print_ip_addr
;; ############################################################################

print_ip_addr:

    ;; DE = VRAM pointer
    ;; HL = IP address
    ;; AF, BC = scratch

    ld    b, #4       ;; loop counter, four octets
00001$:
    push  bc

    ld    a, (hl)
    inc   hl

    cp    a, #10
    jr    c, 00002$        ;; < 10? print only single digit

    ld    b, #100
    cp    a, b
    call  nc, print_div    ;; no hundreds? skip entirely, not even a zero

    ld    b, #10
    call  print_div

00002$:   ;; tens done

    call  print_digit

    pop   bc

    ;; print period?
    dec   b
    ret   z

    ld    a, #'.'
    call  print_char
    jr    00001$           ;; next octet

;; ----------------------------------------------------------------------------
;; Divides A by B, and prints as one digit. Returns remainder in A.
;; Destroys AF'.  '
;; ----------------------------------------------------------------------------

print_div:
    call  a_div_b

    ex    af, af'            ;;'
    ld    a, c

    ;; FALL THROUGH to print_digit


;; ############################################################################

print_digit:
    add  a, #'0'

    ;; FALL THROUGH to print_char


;; ############################################################################
;; _print_char
;; ############################################################################

print_char:

    push hl
    push bc

    add  a, #<((_font_data - 32 * 8) >> 3)
    ld   l, a
    ld   h, #>((_font_data - 32 * 8) >> 3)
    add  hl, hl
    add  hl, hl
    add  hl, hl

    ld   b, #8
    ld   c, d
_print_char_loop:
    ld   a, (hl)
    ld   (de), a
    inc  d
    inc  hl
    djnz _print_char_loop
    ld   d, c

    ex   af, af'            ;;'   bring back A after print_div

    inc  e

    pop  bc
    pop  hl

    ret  nz

    ;; E became zero: means we reached the end of one of the 2K VRAM segments,
    ;; skip to the next one

    ld   a, d
    add  a, #8
    ld   d, a

    ;; A is destroyed here, but this only matters to print_ip_addr, which
    ;; never prints to the end of the line

    ret

;; ############################################################################
;; _print_str
;; ############################################################################

print_str:
    ld   a, (hl)
    inc  hl
    or   a, a
    ret  z
    call print_char
    jr   print_str

