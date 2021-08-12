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
;; heading
;;
;; NOTE: the following opcode (LD L, #n) terminates the string (same as '.')
;; ############################################################################

heading:

    .ascii "SpeccyBoot v"
    .db    VERSION + '0'

    ;; terminated by the following LD L, #n  (0x2E == '.')


;; ############################################################################
;; show_attr_digit_right
;; ############################################################################

show_attr_digit_right:

    ld    l, #24

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

    ;; -----------------------------------------------------------------------
    ;; Return when an empty pixel row is found.
    ;; This will behave badly if DE does not point to correct font data.
    ;; -----------------------------------------------------------------------

    or    a, a
    ret   z

    ;; -----------------------------------------------------------------------
    ;; set B := 7
    ;;     C := (ROW_LENGTH-7)
    ;; -----------------------------------------------------------------------

    ld    bc, #0x0700 + (ROW_LENGTH-7)

show_attr_char_pixel_loop:
    add   a, a
    ld    (hl), #WHITE + (WHITE << 3)
    jr    nc, show_attr_char_pixel_set
    inc   (hl)
show_attr_char_pixel_set:
    inc   hl
    djnz  show_attr_char_pixel_loop

    ;; -----------------------------------------------------------------------
    ;; now B == 0, so BC == C == (ROW_LENGTH-7)
    ;; -----------------------------------------------------------------------

    add   hl, bc                                         ;; next row on screen

    jr    show_attr_digit_row_loop


;; ###########################################################################
;; print_line
;; ###########################################################################

print_line:

    ld   a, (hl)
    cp   a, #'.'
    jr   nz, no_padding
    ld   a, #' '
    .db  JR_NZ            ;; Z is set here, so this will skip the INC HL below

no_padding:

    inc  hl

    call print_char

    ld   a, e
    and  a, #0x1f
    jr   nz, print_line

    ret