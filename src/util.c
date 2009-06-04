/*
 * Module util:
 *
 * Access to ZX Spectrum features (screen, keyboard, joystick, ...)
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
#include <stdbool.h>
#include <stddef.h>

#include "util.h"

#include "platform.h"
#include "logging.h"

/* ------------------------------------------------------------------------- */

const uint16_t zero_u16 = 0u;

/* ------------------------------------------------------------------------- */
/* Screen memory                                                             */
/* ------------------------------------------------------------------------- */

#define BITMAP_BASE   (0x4000)
#define BITMAP_SIZE   (0x1800)
#define ATTRS_BASE    ((BITMAP_BASE) + (BITMAP_SIZE))
#define ATTRS_SIZE    (0x300)

/*
 * defined in splash_screen.c
 */
extern const uint8_t splash_screen[];

/*
 * Offset of a pixel byte in the Spectrum's video memory, as a function of
 * the index (as it would be represented in a 'normal' linear framebuffer)
 */
#define PIXEL_ADDRESS(idx)                                                    \
  ( (((idx) & 0x00E0) << 3) + (HIBYTE(idx) << 5) + ((idx) & 0x001f) )

/* ------------------------------------------------------------------------- */

/*
 * Feature data for digits
 *
 * Features, each one 
 * assigned a value
 * from 0 to 6          Row index
 * -------------------  ---------
 *
 *
 *   .111111.         0
 *   ..1111..         1
 *   0......2         2
 *   00....22         3
 *   00....22         3
 *   00....22         3
 *   00....22         3
 *   0......2         4
 *   ..3333..         5
 *   .333333.         6
 *   ..3333..         7
 *   4......5         8
 *   44....55         9
 *   44....55         9
 *   44....55         9
 *   44....55         9
 *   4......5        10
 *   ..6666..        11
 *   .666666.        12
 */

#define DIGIT_ON      (PAPER(RED) + INK(BLACK) + BRIGHT)
#define DIGIT_OFF     (PAPER(BLACK) + INK(RED))

/*
 * Number of rows in digit image (see above)
 */
#define NBR_ROWS      (13)

/*
 * Features for each digit, where bit 0..6 correspond to the features in the
 * drawing above.
 */
static const uint8_t digit_features[] = {
  0x77,         /* 0 */
  0x24,         /* 1 */
  0x5e,         /* 2 */
  0x6e,         /* 3 */
  0x2d,         /* 4 */
  0x6b,         /* 5 */
  0x7b,         /* 6 */
  0x26,         /* 7 */
  0x7f,         /* 8 */
  0x6f,         /* 9 */
  0x3e,         /* A */
  0x79,         /* b */
  0x58,         /* c */
  0x7c,         /* d */
  0x5b,         /* E */
  0x1b,         /* F */
};

/*
 * For each of the 11 rows, this structure holds the mask each feature
 * contributes to that row.
 *
 * Each row has a length, indicating how many pairs of <feature, mask>
 * that follow.
 */
static const uint8_t feature_rows[] = {
  1,                  1, 0x7e,
  1,                  1, 0x3c,
  2,      0, 0x01,                2, 0x80,
  2,      0, 0x03,                2, 0xc0,
  2,      0, 0x01,                2, 0x80,
  1,                  3, 0x3c,
  1,                  3, 0x7e,
  1,                  3, 0x3c,
  2,      4, 0x01,                5, 0x80,
  2,      4, 0x03,                5, 0xc0,
  2,      4, 0x01,                5, 0x80,
  1,                  6, 0x3c,
  1,                  6, 0x7e
};

/* ------------------------------------------------------------------------- */

/*
 * Write 10 bits to the screen (attributes).
 *
 * bits:    Bitmask, indicating which of the 8 bits are set. Bit 0 represents
 *          the leftmost attribute cell.
 */
static void
display_digit_row(uint8_t bits, uint8_t *start_address)
{
  uint8_t i;
  for (i = 0; i < 8; i++) {
    *start_address ++ = (bits & 0x01) ? (DIGIT_ON) : (DIGIT_OFF);
    bits >>= 1;
  }
}

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
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
    if (i == 3 || i == 9) {
      uint8_t j;
      for (j = 0; j < 5; j++) {
        display_digit_row(bits, start_address);
        start_address += ROW_LENGTH;
      }
    }
  }
}

/* ------------------------------------------------------------------------- */

/*
 * Store a byte in the Spectrum's video memory using a linear index
 */
static void
store_splash_byte(uint8_t b, uint16_t dst_index)
{
  *((uint8_t *) BITMAP_BASE + 0x0800 + PIXEL_ADDRESS(dst_index)) = b;
}

/* -------------------------------------------------------------------------
 * RST30 entry point (software interrupt)
 * ------------------------------------------------------------------------- */

void
rst30_handler(void)
__naked
{
  __asm
  
    ;; TODO: add some hooks for secondary loader here
    
    ret
    
  __endasm;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void
display_splash(void)
{
  uint16_t dst_index = 0;
  uint16_t src_index = 0;
  const uint8_t *src = splash_screen;
  
  while(dst_index < 0x0800) {
    uint8_t b = *src++;
    if (b) {
      store_splash_byte(b, dst_index ++);
    }
    else {
      uint8_t b = *src++;
      if (b) {    /* NUL followed by a non-NUL byte */
        store_splash_byte(0, dst_index ++);
        store_splash_byte(b, dst_index ++);
      }
      else {
        uint16_t len = *src++ + 2;
        while (len) {
          store_splash_byte(0, dst_index ++);
          len --;
        }
      }
    }
  }
}

/* ------------------------------------------------------------------------- */

void
select_bank(uint8_t bank_id)
{
  static Z80_PORT(0x7FFD) bank_selection;
  
  bank_selection = bank_id;
}

/* ------------------------------------------------------------------------- */

void
display_digits(uint8_t value)
{
  static uint8_t current_digit_1 = 0x10;    /* force update first time */
  static uint8_t current_digit_2 = 0x10;    /* force update first time */
  
  uint8_t digit1 = (value / 10);
  uint8_t digit2 = (value % 10);
  
  if (digit1 != current_digit_1) {
    display_digit_at(digit1, (uint8_t *) ATTRS_BASE + 7);
    current_digit_1 = digit1;
  }
  if (digit2 != current_digit_2) {
    display_digit_at(digit2, (uint8_t *) (ATTRS_BASE + 17));
    current_digit_2 = digit2;
  }
  
#if 0
  {
    uint8_t i;
    for (i = 0; i < 50; i++) {
      __asm
      halt
      __endasm;
    }
  }
#endif
}

/* ------------------------------------------------------------------------- */

void
set_border(uint8_t border_attrs)
{
  static Z80_PORT(0x00fe) ula_port;

  Z80_PORT_WRITE(ula_port, border_attrs & 0x07);
}

/* ------------------------------------------------------------------------- */

void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint16_t n)
{
  uint16_t i;
  uint8_t *p = ((uint8_t *) ATTRS_BASE) + col + (row * ROW_LENGTH);
  for (i = 0; i < n; i++) {
    *p++ = screen_attrs;
  }
}

/* ------------------------------------------------------------------------- */

void
fatal_error(uint8_t error_code)
{
  logging_add_entry("FATAL ERROR 0x" HEX8_ARG, &error_code);
#if 0 //ndef VERBOSE_LOGGING
  /*
   * Only display error number if loggging is disabled, to avoid writing over
   * interesting logged information
   */
  set_screen_attrs(PAPER(BLACK) + INK(BLACK));
  display_digit_at(0x0E, (uint8_t *) ATTRS_BASE);
  display_digit_at(error_code, (uint8_t *) (ATTRS_BASE + 17));
#endif
  
  __asm

    ;; Display infinite psychedelic border pattern

    di
  panic_loop::
    ld  a, r
    and #7      ;; stay away from loadspeaker bit...
    out (0xFE), a
    jr panic_loop
  
  __endasm;
}
