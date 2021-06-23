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


;; ############################################################################
;; fail
;; fail_timeout
;; ############################################################################

    .area _CODE

fail_timeout:

    ld    a, #FATAL_NO_RESPONSE

    ;; FALL THROUGH to fail

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
;; memory_compare
;; memory_compare_4_bytes
;; ############################################################################
  
    .area _CODE

memory_compare_4_bytes:
    ld   b, #4

memory_compare:
    ld   a, (de)
    cp   a, (hl)
    ret  nz
    inc  de
    inc  hl
    djnz memory_compare
    ret
