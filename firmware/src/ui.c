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

    ld    l, 1(ix)
    ld    h, 2(ix)   ;; HL = start_address

    ld    de, #_font_data + 8 * 16 + 1   ;; address of '0' bits
    ld    a, 0(ix)
    sla   a
    sla   a
    sla   a
    add   a, e
    ld    e, a

    ;; no need to worry about carry (to register D) in this addition,
    ;; since all digit pixels are stored in the range 0x5F31 .. 0x5FFF
    ;; (that is, D is always 0x5F)

    ld    c, #6
row_loop::
    ld    a, (de)
    sla   a
    inc   de
    ld    b, #6
pixel_loop::
    sla   a
    jr    nc, skip_pixel
    ld    (hl), #PAPER(WHITE) + INK(WHITE)
    jr    pixel_done
skip_pixel::
    ld    (hl), #PAPER(BLACK) + INK(BLACK)
pixel_done::
    inc   hl
    djnz  pixel_loop

    ld    a, c
    ld    bc, #(ROW_LENGTH-6)
    add   hl, bc
    ld    c, a
    dec   c
    jr    nz, row_loop

    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

// 2n/3 can be calculated as 43n/64 for 0 <= n <= 48
#define TWO_THIRDS(x) ( ( ((x) << 5) + ((x) << 3) + ((x) << 1) + (x) ) >> 6 )

void
display_progress(uint8_t kilobytes_loaded,
                 uint8_t kilobytes_expected)
{
  uint8_t progress = (kilobytes_expected == 48)
                     ? TWO_THIRDS(kilobytes_loaded)
                     : (kilobytes_loaded >> 2);

  if (progress) {
    *((uint8_t *) PROGRESS_BAR_BASE - 1 + progress)
      = PAPER(BLUE) + INK(BLUE) + BRIGHT;
  }
}
