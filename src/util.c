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

// #define DIGIT_WIDTH   (15)
#define DIGIT_ON      (PAPER(GREEN) + INK(GREEN) + BRIGHT)
#define DIGIT_OFF     (PAPER(BLACK) + INK(BLACK))

#define BAR_LEFT      (0x0001)
#define BAR_CENTER    (0x0002)
#define BAR_RIGHT     (0x0004)

/*
 * Macro to encode all features of a digit into a single 16-bit (actually,
 * 15-bit) value
 */
#define DIGIT_DATA(head, upper, center, lower, bottom)                        \
(((head) << 12) | ((upper) << 9) | ((center) << 6) | ((lower) << 3) | bottom)

/*
 * Macros to pick out the features of a digit
 */
#define DIGIT_HEAD(d)     ((d) >> 12)
#define DIGIT_UPPER(d)    ((d) >> 9)
#define DIGIT_CENTER(d)   ((d) >> 6)
#define DIGIT_LOWER(d)    ((d) >> 3)
#define DIGIT_BOTTOM(d)   (d)

/*
 * Index of letter 'E' in digit_features[] below
 */
#define DIGIT_E_INDEX     (10)

/* ------------------------------------------------------------------------- */

/*
 * Feature data for ten decimal digits, plus E for error
 *
 * Features, each described as a bitmask of BAR_{LEFT|CENTER|RIGHT}:
 *
 *   XXXXXXXXXXXXXXX   \
 *   XXXXXXXXXXXXXXX   |  Head
 *   XXXXXXXXXXXXXXX   |  (4 rows)
 *   XXXXXXXXXXXXXXX   /
 *   XXXX.......XXXX   \
 *   XXXX.......XXXX   |
 *   XXXX.......XXXX   |  Upper
 *   XXXX.......XXXX   |  (6 rows)
 *   XXXX.......XXXX   |
 *   XXXX.......XXXX   /
 *   XXXXXXXXXXXXXXX   \
 *   XXXXXXXXXXXXXXX   |  Center
 *   XXXXXXXXXXXXXXX   |  (4 rows)
 *   XXXXXXXXXXXXXXX   /
 *   XXXX.......XXXX   \
 *   XXXX.......XXXX   |
 *   XXXX.......XXXX   |  Lower
 *   XXXX.......XXXX   |  (6 rows)
 *   XXXX.......XXXX   |
 *   XXXX.......XXXX   /
 *   XXXXXXXXXXXXXXX   \
 *   XXXXXXXXXXXXXXX   |  Bottom
 *   XXXXXXXXXXXXXXX   |  (4 rows)
 *   XXXXXXXXXXXXXXX   /
 */

static const uint16_t digit_features[] = {
DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 0 */
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT,                              /* 1 */
           BAR_LEFT,
           BAR_LEFT,
           BAR_LEFT,
           BAR_LEFT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 2 */
                                   BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
           BAR_LEFT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 3 */
                                   BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
                                   BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT              + BAR_RIGHT,     /* 4 */
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
                                   BAR_RIGHT,
                                   BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 5 */
           BAR_LEFT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
                                   BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 6 */
           BAR_LEFT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 7 */
                                   BAR_RIGHT,
                                   BAR_RIGHT,
                                   BAR_RIGHT,
                                   BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 8 */
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* 9 */
           BAR_LEFT              + BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
                                   BAR_RIGHT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT),

DIGIT_DATA(BAR_LEFT + BAR_CENTER + BAR_RIGHT,     /* E */
           BAR_LEFT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT,
           BAR_LEFT,
           BAR_LEFT + BAR_CENTER + BAR_RIGHT)
};

/* ------------------------------------------------------------------------- */

/*
 * Write 15 bits to the screen (attributes).
 *
 * bits:    Bitmask, where bits 0-2 indicate which parts to set
 *          (as a sum of BAR_LEFT, BAR_CENTER, BAR_RIGHT)
 */
static void
display_digit_row(uint8_t bits, uint8_t *start_address)
{
  uint8_t screen_value;

  screen_value = (bits & BAR_LEFT) ? (DIGIT_ON) : (DIGIT_OFF);
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  
  screen_value = (bits & BAR_CENTER) ? (DIGIT_ON) : (DIGIT_OFF);
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  
  screen_value = (bits & BAR_RIGHT) ? (DIGIT_ON) : (DIGIT_OFF);
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address ++ = screen_value;
  *start_address    = screen_value;
}

/* ------------------------------------------------------------------------- */

/*
 * Display a decimal digit
 */
/* static */ void
display_digit_at(uint8_t digit, uint8_t *start_address)
{
  uint16_t features = digit_features[digit];
  uint8_t i;
  
  uint8_t bits = DIGIT_HEAD(features);
  for (i = 0; i < 4; i++) {
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
  }
  
  bits = DIGIT_UPPER(features);
  for (i = 0; i < 6; i++) {
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
  }
  
  bits = DIGIT_CENTER(features);
  for (i = 0; i < 4; i++) {
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
  }
  
  bits = DIGIT_LOWER(features);
  for (i = 0; i < 6; i++) {
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
  }
  
  bits = DIGIT_BOTTOM(features);
  for (i = 0; i < 4; i++) {
    display_digit_row(bits, start_address);
    start_address += ROW_LENGTH;
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
display_digits(uint8_t value)
{
  static uint8_t current_digit_1 = 0x10;    /* force update */
  static uint8_t current_digit_2 = 0x10;    /* force update */
  
  uint8_t digit1 = (value / 10);
  uint8_t digit2 = (value % 10);
  
  if (digit1 != current_digit_1) {
    display_digit_at(digit1, (uint8_t *) ATTRS_BASE);
    current_digit_1 = digit1;
  }
  if (digit2 != current_digit_2) {
    display_digit_at(digit2, (uint8_t *) (ATTRS_BASE + 17));
    current_digit_2 = digit2;
  }
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
#ifndef VERBOSE_LOGGING
  /*
   * Only display error number if loggging is disabled, to avoid writing over
   * interesting logged information
   */
  set_screen_attrs(PAPER(BLACK) + INK(BLACK));
  display_digit_at(DIGIT_E_INDEX, (uint8_t *) ATTRS_BASE);
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
