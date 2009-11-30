/*
 * Module util:
 *
 * Miscellaneous utility functions, including access to ZX Spectrum features
 * (screen, keyboard, sound, ...)
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
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

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "util.h"
#include "eth.h"

#include "syslog.h"

#include "rxbuffer.h"

/* ------------------------------------------------------------------------- */
/* Screen memory                                                             */
/* ------------------------------------------------------------------------- */

#define PROGRESS_BASE   ((ATTRS_BASE) + 0x220)

/* ------------------------------------------------------------------------- */

/*
 * Feature data for digits
 *
 * Features, each one 
 * assigned a value
 * from 0 to 6        Row index
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
  4,      0, 0x01,    1, 0xff,    2, 0x80,    7, 0x40,
  2,      0, 0x01,                2, 0x80,
  5,      0, 0x01,    3, 0xff,    2, 0x80,
          4, 0x01,                5, 0x80,
  2,      4, 0x01,                5, 0x80,
  3,      4, 0x01,    6, 0xff,    5, 0x80
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
  13, 0x4c, 0x4b, 0x4a, 0x48,     /* BFFE: enter, 'L', 'K', 'J', 'H' */
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
 * Buffer for font data: stored in an absolute position outside of the
 * runtime data. This means that the font data will be written over by the
 * loaded snapshot.
 */
static uint8_t __at(0x7000) font_data[0x300];

/* ------------------------------------------------------------------------- */

/*
 * Tick count, increased by the 50Hz timer ISR in crt0.asm
 */
volatile timer_t timer_tick_count = 0;

/* ------------------------------------------------------------------------- */

/*
 * Display a digit
 */
static void
display_digit_at(uint8_t digit, uint8_t *start_address)
{
  uint8_t i;
  uint8_t *src_row_ptr = feature_rows;
  
  for (i = 0; i < NBR_ROWS; i ++) {
    uint8_t nbr_features_in_row = *src_row_ptr ++;
    uint8_t j;
    uint8_t bits = 0;
    for (j = 0; j < nbr_features_in_row; j++) {
      uint8_t feature = *src_row_ptr ++;
      uint8_t mask    = *src_row_ptr ++;
      if (digit_features[digit] & (1 << (feature))) {
        bits |= mask;
      }
    }

    for (j = 0; j < 8; j++) {
      *start_address ++ = (bits & 0x01) ? (PAPER(WHITE) + INK(WHITE))
                                        : (PAPER(BLACK) + INK(BLACK));
      bits >>= 1;
    }
    
    start_address += (ROW_LENGTH - 8);
  }
}

/* ------------------------------------------------------------------------- */

static void
display_digits(bool three_digits,
               uint8_t digit1, uint8_t digit2, uint8_t digit3)
{
  /*
   * Always assume last digit needs updating
   */
  static uint8_t current_digit_1 = 0x10;    /* force update first time */
  static uint8_t current_digit_2 = 0x10;    /* force update first time */
  
  if (three_digits) {
    if (digit1 != current_digit_1) {
      display_digit_at(digit1, (uint8_t *) PROGRESS_BASE + 2);
      current_digit_1 = digit1;
    }
    if (digit2 != current_digit_2) {
      display_digit_at(digit2, (uint8_t *) (PROGRESS_BASE + 12));
      current_digit_2 = digit2;
    }
    display_digit_at(digit3, (uint8_t *) (PROGRESS_BASE + 22));
  }
  else {
    if (digit1 != current_digit_1) {
      display_digit_at(digit1, (uint8_t *) PROGRESS_BASE + 7);
      current_digit_1 = digit1;
    }
    display_digit_at(digit2, (uint8_t *) (PROGRESS_BASE + 17));
  }
}

/* ------------------------------------------------------------------------- */

/*
 * Code snippet for loading font data from ROM to RAM. This snippet will be
 * copied to RAM before execution, so it cannot use any absolute addressing.
 */
static void
font_data_loader(void)
__naked
{
  __asm
  
  di

  ld    a, #0x28
  out   (0x9f), a   ;; page out SpeccyBoot, keep ETH in reset
  
  ld    a, #0x10    ;; page in ROM1, page 0, no lock
  ld    bc, #0x7ffd
  out   (c), a      ;; page in ROM1 (48k BASIC)
  
  ld    hl, #0x3d00 ;; address of font data in ROM1
  ld    de, #_font_data
  ld    bc, #0x0300
  ldir

  ld    a, #0x08
  out   (0x9f), a   ;; page in SpeccyBoot

  ei
  ret
  
  ;;
  ;; Label necessary for copying to RAM
  ;;
end_of_font_data_loader::
  
  __endasm;
}

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
  
  ld  hl, #BITMAP_BASE
  ld  de, #BITMAP_BASE+1
  ld  bc, #BITMAP_SIZE+ATTRS_SIZE-1
  ld  (hl), #0
  ldir
  ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
display_progress(uint8_t kilobytes_loaded, uint8_t kilobytes_expected)
{
  {
    /*
     * Progress bar
     */
    uint8_t progress = (((uint16_t) kilobytes_loaded) << 5) / kilobytes_expected;
    set_attrs(PAPER(WHITE) + INK(WHITE), 23, 0, progress);
    set_attrs(PAPER(BLUE) + INK(BLUE) | BRIGHT, 23, progress, 32 - progress);
  }

  {
    bool three_digits = (kilobytes_expected >= 100);
    uint8_t digit1;
    uint8_t digit2;
    uint8_t digit3 = 0;
    
    if (three_digits) {
      digit1 = (kilobytes_loaded / 100);
      digit2 = ((kilobytes_loaded / 10) % 10);
      digit3 = (kilobytes_loaded % 10);
    }
    else {
      digit1 = (kilobytes_loaded / 10);
      digit2 = (kilobytes_loaded % 10);
    }
    
    display_digits(three_digits, digit1, digit2, digit3);
  }
}

/* ------------------------------------------------------------------------- */

void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint8_t n)
{
  uint8_t i;
  uint8_t *p = ((uint8_t *) ATTRS_BASE) + col + (row * ROW_LENGTH);
  for (i = 0; i < n; i++) {
    *p++ = screen_attrs;
  }
}

/* ------------------------------------------------------------------------- */

void
load_font_data(void)
{
  __asm
  
  ;; copy the funtion 'font_data_loader' to RAM (0x7300) and call it.
  
  ld  hl, #_font_data_loader
  ld  de, #_font_data + 0x0300
  ld  bc, #end_of_font_data_loader - _font_data_loader
  ldir
  
  call    _font_data + 0x0300
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
print_at(uint8_t line, uint8_t column, const char *s)
{
  char c;
  while (c = *s++) {
    print_char_at(line, column ++, c);
  }
}

/* ------------------------------------------------------------------------- */

void
print_char_at(uint8_t row, uint8_t column, char c)
{
  print_pattern_at(row, column, FONTDATA_ADDRESS(c));
}

/* ------------------------------------------------------------------------- */

void
print_pattern_at(uint8_t row, uint8_t col, const uint8_t *pattern)
__naked
{
  (void) row, col, pattern;         /* avoid warnings about unused arguments */
  
  __asm
  
    ;; assume row             at (sp + 2)
    ;;        col             at (sp + 3)
    ;;        LOBYTE(pattern) at (sp + 4)
    ;;        HIBYTE(pattern) at (sp + 5)
    ;;
    ;; use:
    ;;
    ;; de = destination in VRAM
    ;; hl = pattern
    
    ld    hl, #2
    add   hl, sp
    
    ;; compute d as 0x40 + (row & 0x18)
    
    ld    a, (hl)    ;; row
    and   a, #0x18
    add   a, #0x40
    ld    d, a
    
    ;; compute e as ((row & 7) << 5) + col
    
    ld    a, (hl)   ;; row
    inc   hl        ;; now points to col
    and   a, #7
    rrca            ;; rotate right by 3 == rotate left by 5, for values <= 7
    rrca
    rrca
    add   a, (hl)   ;; col
    inc   hl        ;; now points to LOBYTE(pattern)
    ld    e, a
    
    ld    c, (hl)
    inc   hl
    ld    h, (hl)
    ld    l, c
    
    ld    b, #8
print_pattern_at_loop::
    ld    a, (hl)
    ld    (de), a
    inc   d
    inc   hl
    djnz  print_pattern_at_loop
    
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
print_pattern_with_attrs_at(uint8_t attrs,
                            uint8_t row,
                            uint8_t col,
                            const uint8_t *pattern)
{
  print_pattern_at(row, col, pattern);
  set_attrs(attrs, row, col, 1);
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

timer_t
timer_value(timer_t timer)
{
  timer_t current_value;
  DISABLE_INTERRUPTS;
  current_value = timer_tick_count;
  ENABLE_INTERRUPTS;
  return current_value - timer;
}

/* ------------------------------------------------------------------------- */

uint8_t
rand5bits(void)
__naked
{
  __asm
  
    ld    a, r
    add   a, #MAC_ADDR_0+MAC_ADDR_1+MAC_ADDR_2+MAC_ADDR_3+MAC_ADDR_4+MAC_ADDR_5
    and   a, #0x1f
    ld    l, a
  
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
fatal_error(const char *message)
{
  cls();

  load_font_data();
  
  print_at(4, 10, "Fatal error");
  set_attrs(INK(BLUE) | PAPER(WHITE) | BRIGHT | FLASH, 4, 9, 13);
  
  print_at(16, (32 - strlen(message)) >> 1, message);
  set_attrs(INK(WHITE) | PAPER(BLACK) | BRIGHT, 16, 0, 32);
  
  eth_init();
  syslog(message);

  __asm
    di
    halt
  __endasm;
}
