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
    sub  a, (hl)
    ret  nz
    inc  de
    inc  hl
    djnz memory_compare
    ret


;; ############################################################################
;; title_str
;; ############################################################################

   .area _CODE

title_str:
    .ascii "SpeccyBoot "
    .db   VERSION_STAGE1 + '0'

    ;; terminated by '.', which is the LD L, #n instruction below

;; ############################################################################
;; show_attr_digit_right
;; ############################################################################

show_attr_digit_right:

    ld    l, #24                               ;; NOTE: terminates string above

   ;; FALL THROUGH to show_attr_digit


;; ############################################################################
;; show_attr_digit
;; ############################################################################

show_attr_digit:

    add   a, a
    add   a, a
    add   a, a

show_attr_digit_already_shifted:

    ;; -----------------------------------------------------------------------
    ;; Font data is assumed to start on a page boundary, which means that the
    ;; data for digit '0' starts ('0'-' ')*8 = 0x80 bytes in.
    ;;
    ;; However, A may already have bit 7 set (spilling over from the least-
    ;; significant bit of the next digit). Use OR rather than ADD.
    ;; -----------------------------------------------------------------------

    or    a, #0x80
    ld    d, #>_font_data
    ld    e, a
    ld    h, #>ATTR_DIGIT_ROW

show_attr_digit_row_loop:
    inc   de
    ld    a, (de)
    ld    b, #7

show_attr_char_pixel_loop:
    add   a, a
    ld    (hl), #BLACK + (BLACK << 3)
    jr    c, show_attr_char_pixel_set
    ld    (hl), #WHITE + (WHITE << 3)
show_attr_char_pixel_set:
    inc   hl
    djnz  show_attr_char_pixel_loop

    ld    a, #(ROW_LENGTH-7)
    add   a, l
    ld    l, a

    cp    a, #ROW_LENGTH * 6
    jr    c, show_attr_digit_row_loop

    ret

;; ############################################################################
;; print_str
;; ############################################################################

    .area _CODE

print_str:

    ld   a, (hl)
    cp   a, #'.'
    jr   nz, no_padding
    ld   a, #' '
    .db  JR_NZ          ;; Z is set here, so this will skip the INC HL below

no_padding:

    inc  hl

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
