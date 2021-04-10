/*
 * Module ui:
 *
 * Access to ZX Spectrum display, keyboard, and sound.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ui.h"

#include "globals.h"

/* ------------------------------------------------------------------------- */

/*
 * Feature data for digits
 *
 * Features, each one
 * assigned a value
 * from 0 to 7        Row index
 * -----------------  ---------
 *
 *
 *   x11111zy         0   x=0&1, y=1&2, z=1&2&7
 *   0......2         1
 *   x333333y         2   x=0&3&4, y=2&3&5
 *   4......5         3
 *   x666666y         4   x=4&6, y=5&6
 */

/*
 * Number of rows in digit image (see above)
 */
#define NBR_ROWS      (5)

/*
 * Features for each digit, where bit 0..7 correspond to the features in the
 * drawing above.
 */
static const uint8_t digit_features[] = {
  0x77,          /* 0 */
  0xa4,          /* 1 */
  0x5e,          /* 2 */
  0x6e,          /* 3 */
  0x2d,          /* 4 */
  0x6b,          /* 5 */
  0x7b,          /* 6 */
  0x26,          /* 7 */
  0x7f,          /* 8 */
  0x6f           /* 9 */
};

/*
 * For each of the 5 rows, this structure holds the mask each feature
 * contributes to that row.
 *
 * Each row has a length, indicating how many pairs of <feature, mask>
 * that follow.
 */
static const uint8_t feature_rows[] = {
  4,      (1 << 0), 0x01,  (1 << 1), 0xff,  (1 << 2), 0x80,  0x80, 0x40,
  2,      (1 << 0), 0x01,                   (1 << 2), 0x80,
  5,      (1 << 0), 0x01,  (1 << 3), 0xff,  (1 << 2), 0x80,
          (1 << 4), 0x01,                   (1 << 5), 0x80,
  2,      (1 << 4), 0x01,                   (1 << 5), 0x80,
  3,      (1 << 4), 0x01,  (1 << 6), 0xff,  (1 << 5), 0x80
};

/* ----------------------------------------------------------------------------
 * Keyboard mapping (used by _poll_key below)
 *
 * ZX Spectrum BASIC Programming (Vickers), Chapter 23:
 *
 * IN 65278 reads the half row CAPS SHIFT to V
 * IN 65022 reads the half row A to G
 * IN 64510 reads the half row Q to T
 * IN 63486 reads the half row 1 to 5
 * IN 61438 reads the half row O to 6
 * IN 57342 reads the half row P to 7
 * IN 49150 reads the half row ENTER to H
 * IN 32766 reads the half row SPACE to B
 *
 * http://www.worldofspectrum.org/ZXBasicManual/index.html
 *
 * A '0' in the 'key_rows' table means that key is to be ignored. The rows
 * are ordered for the high byte in the row address to take values in the
 * following order:
 *
 * 01111111
 * 10111111
 * 11011111
 * 11101111
 * 11110111
 * 11111011
 * 11111101
 * 11111110
 *-------------------------------------------------------------------------- */

static const uint8_t key_rows[] = {
  0x20, 0, 0x4d, 0x4e, 0x42,      /* 7FFE: space, shift, 'M', 'N', 'B' */
  0x0d, 0x4c, 0x4b, 0x4a, 0x48,   /* BFFE: enter, 'L', 'K', 'J', 'H' */
  0x50, 0x4f, 0x49, 0x55, 0x59,   /* DFFE: 'P', 'O', 'I', 'U', 'Y' */
  0x30, 0x39, 0x38, 0x37, 0x36,   /* EFFE: '0', '9', '8', '7', '6' */
  0x31, 0x32, 0x33, 0x34, 0x35,   /* F7FE: '1', '2', '3', '4', '5' */
  0x51, 0x57, 0x45, 0x52, 0x54,   /* FBDE: 'Q', 'W', 'E', 'R', 'T' */
  0x41, 0x53, 0x44, 0x46, 0x47,   /* FDFE: 'A', 'S', 'D', 'F', 'G' */
  0, 0x5a, 0x58, 0x43, 0x56,      /* FEFE: shift, 'Z', 'X', 'C', 'V' */
};

/* ----------------------------------------------------------------------------
 * Multiplication table for multiplication by 2/3, for values 0..48.
 *-------------------------------------------------------------------------- */

static const uint8_t scaled_progress[49] = {
  /* Table for multiplication by 2/3, for values 0..48. */
  0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x04,
  0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08,
  0x09, 0x0a, 0x0a, 0x0b, 0x0c, 0x0c, 0x0d,
  0x0e, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x12,
  0x12, 0x13, 0x14, 0x14, 0x15, 0x16, 0x16,
  0x17, 0x18, 0x18, 0x19, 0x1a, 0x1a, 0x1b,
  0x1c, 0x1c, 0x1d, 0x1e, 0x1e, 0x1f, 0x20
};

/* ------------------------------------------------------------------------- */

/*
 * Repeat time-outs: between the keypress and the first repetition, and for
 * any subsequent repetitions
 *
 * (measured in ticks of 20ms)
 */
#define REPEAT_FIRST_TIMEOUT    (20)
#define REPEAT_NEXT_TIMEOUT     (5)

/* ------------------------------------------------------------------------- */

/*
 * Poll keyboard: return currently pressed key, or KEY_NONE
 */
static char
poll_key(void)
__naked
{
  __asm

    ld    hl, #_key_rows
    ld    bc, #0x7ffe
poll_outer::
    in    d, (c)

    ld    e, #5       ;; number of keys in each row

poll_inner::
    ld    a, (hl)
    inc   hl
    rr    d
    jr    c, not_pressed
    or    a
    jr    nz, poll_done

not_pressed::
    dec   e
    jr    nz, poll_inner

    rrc   b
    jr    c, poll_outer

    xor   a         ;; KEY_NONE == 0

poll_done::
    ld    l, a

    ret

  __endasm;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void
cls(void)
__naked
{
    __asm

    xor a
    out (0xfe), a
    ld  hl, #0x4000
    ld  de, #0x4001
    ld  bc, #0x1AFF
    ld  (hl), a
    ldir
    ret

    __endasm;
}

/* ------------------------------------------------------------------------- */

void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint8_t n)
__naked
{
  (void) screen_attrs, row, col, n;

  __asm

    ;; assume attrs           at (sp + 4)
    ;;        row             at (sp + 5)
    ;;        col             at (sp + 6)
    ;;        n               at (sp + 7)

    push  ix

    ld    ix, #0
    add   ix, sp

    ld    l, 5(ix)  ;; row
    ld    h, #0
    add   hl, hl
    add   hl, hl
    add   hl, hl
    add   hl, hl
    add   hl, hl
    ld    a, 6(ix)
    add   l
    ld    l, a
    ld    bc, #0x5800
    add   hl, bc    ;; HL now points to attribute VRAM

    ld    a, 4(ix)
    ld    b, 7(ix)
set_attrs_loop::
    ld    (hl), a
    inc   hl
    djnz  set_attrs_loop

    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
print_at(uint8_t row,
	 uint8_t start_col,
	 uint8_t end_col,
	 char terminator,
	 const char *s)
__naked
{
  (void) row, start_col, end_col, terminator, s;

  __asm

    ;; assume row             at (sp + 4)
    ;;        start_col       at (sp + 5)
    ;;        end_col         at (sp + 6)
    ;;        terminator      at (sp + 7)
    ;;        LOBYTE(s)       at (sp + 8)
    ;;        HIBYTE(s)       at (sp + 9)
    ;;
    ;; use:
    ;;
    ;; de = destination in VRAM
    ;; hl = pattern

    push  ix

    ld    ix, #0
    add   ix, sp

    ;; compute d as 0x40 + (row & 0x18)

    ld    a, 4(ix)   ;; row
    and   a, #0x18
    add   a, #0x40
    ld    d, a

    ;; compute e as ((row & 7) << 5) + start_col

    ld    a, 4(ix)  ;; row
    and   a, #7
    rrca            ;; rotate right by 3 == rotate left by 5, for values <= 7
    rrca
    rrca
    add   a, 5(ix)  ;; start_col
    ld    e, a

    ld    c, 8(ix)
    ld    b, 9(ix)

    push  bc
    pop   iy        ;; now holds s

string_loop::
    ld    a, e
    and   #0x1f     ;; current column
    cp    6(ix)     ;; end_col
    jr    nc, print_done

    ld    a, (iy)
    cp    7(ix)     ;; terminator
    jr    z, string_padding
    inc   iy

    sub   a, #32
    ld    h, #0
    ld    l, a
    add   hl, hl
    add   hl, hl
    add   hl, hl
    ld    bc, #_font_data
    add   hl, bc

    ld    b, #8
    push  de
char_loop::
    ld    a, (hl)
    ld    (de), a
    inc   d
    inc   hl
    djnz  char_loop

    pop   de
    inc   e
    jr    string_loop

string_padding::
    ld    a, e
    and   #0x1f     ;; current column
    cp    6(ix)     ;; end_col
    jr    nc, print_done

    ld    b, #8
    push  de
    xor   a
space_loop::
    ld    (de), a
    inc   d
    djnz  space_loop

    pop   de
    inc   e
    jr    string_padding

print_done::
    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

key_t
wait_key(void)
{
  static timer_t repeat_timer;
  static key_t previous_key = KEY_NONE;
  static bool first_repetition;

  key_t key = poll_key();
  if (key != KEY_NONE && key == previous_key) {
    /*
     * The previous key is still being pressed. See if it remains
     * pressed until the repetition timer expires.
     */
    while(poll_key() == previous_key) {
      if ((first_repetition
           && timer_value(repeat_timer) >= REPEAT_FIRST_TIMEOUT)
          || ((! first_repetition)
              && timer_value(repeat_timer) >= REPEAT_NEXT_TIMEOUT))
      {
        timer_reset(repeat_timer);
        first_repetition = false;
        return previous_key;
      }
    }
  }
  do {
    key = poll_key();
  } while (key == KEY_NONE);

  timer_reset(repeat_timer);
  previous_key = key;
  first_repetition = true;
  return key;
}

/* ------------------------------------------------------------------------- */

void
key_click(void)
__naked
{
  __asm

  ld    bc, #0x14FE
  ld    d, #0x10
  ld    a, d
  di
keyclick_loop::
  out   (c), a
  xor   a, d
  djnz  keyclick_loop
  ei
  ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Display a digit
 */
void
display_digit_impl(uint8_t digit, uint8_t *start_address)
__naked
{
  (void) digit, start_address;

  __asm

    push  ix

    ld    ix, #4
    add   ix, sp

    ld    e, 1(ix)
    ld    d, 2(ix)   ;; DE'=start_address   '

    push  de
    pop   iy

    ld    hl, #_digit_features
    ld    a, l
    add   a, 0(ix)
    ld    l, a
    jr    nc, no_carry_for_digit_calc   ;; FIXME avoid this
    inc   h
no_carry_for_digit_calc::
    ld    d, (hl)   ;; D now holds digit features

    ;; Iterate over feature_rows, determine bitmask to display

    ld    hl, #_feature_rows
    ld    c, #NBR_ROWS
row_loop::
    xor   a
    ex    af, af'   ;; A' holds bitmask to show
    ld    b, (hl)   ;; B now holds number of features for this row
    inc   hl
feature_loop::
    ld    e, (hl)   ;; E holds a feature in this row
    inc   hl        ;; (HL) holds the mask for this feature

    ld    a, d
    and   e
    jr    z, no_feature
    ex    af, af'   ;; A' holds bitmask to show
    or    (hl)
    ex    af, af'   ;; put back A'
no_feature::
    inc   hl
    djnz  feature_loop

    ;; Display bitmask in alternate A register

    push  bc
    ld    b, #8
    ex    af, af'   ;; A' holds bitmask to show
    ld    c, a
bitmask_loop::
    ld    a, #PAPER(BLACK) + INK(BLACK)
    rr    c
    jr    nc, bit_not_set
    ld    a, #PAPER(WHITE) + INK(WHITE)
bit_not_set::
    ld    (iy), a
    inc   iy
    djnz  bitmask_loop

    ld    bc, #(ROW_LENGTH-8)
    add   iy, bc
    pop   bc

    dec   c
    jr    nz, row_loop

    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
display_progress(uint8_t kilobytes_loaded,
		 uint8_t kilobytes_expected)
{
  uint8_t progress = (kilobytes_expected == 48)
                     ? scaled_progress[kilobytes_loaded]
                     : (kilobytes_loaded >> 2);

  if (progress) {
    *((uint8_t *) PROGRESS_BAR_BASE - 1 + progress)
      = PAPER(WHITE) + INK(WHITE) + BRIGHT;
  }
}
