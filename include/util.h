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

#ifndef SPECCYBOOT_UTIL_INCLUSION_GUARD
#define SPECCYBOOT_UTIL_INCLUSION_GUARD

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Error messages (passed to fatal_error())
 * ------------------------------------------------------------------------- */

#define FATAL_ERROR_INTERNAL        (0)
#define FATAL_ERROR_TX_FAIL         (1)
#define FATAL_ERROR_SPI_POLL_FAIL   (2)
#define FATAL_ERROR_RX_PTR_FAIL     (3)
#define FATAL_ERROR_INCOMPATIBLE    (4)
#define FATAL_ERROR_END_OF_DATA     (5)

/* -------------------------------------------------------------------------
 * Spectrum attributes
 * ------------------------------------------------------------------------- */

#define BLACK         (0x00)
#define BLUE          (0x01)
#define RED           (0x02)
#define MAGENTA       (0x03)
#define GREEN         (0x04)
#define CYAN          (0x05)
#define YELLOW        (0x06)
#define WHITE         (0x07)

#define BRIGHT        (0x40)
#define FLASH         (0x80)

#define INK(x)        (x)
#define PAPER(x)      ((x) << 3)

#define ROW_LENGTH    (32)

/* -------------------------------------------------------------------------
 * A simple zero value -- useful for passing pointers to zero
 * ------------------------------------------------------------------------- */

extern const uint16_t zero_u16;

/* -------------------------------------------------------------------------
 * Display splash screen
 * ------------------------------------------------------------------------- */
void
display_splash(void);

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col). If (col+n) extends
 * beyond the end of the row, changes will continue at the beginning of the
 * following row.
 * ------------------------------------------------------------------------- */
void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint16_t n);

/* ------------------------------------------------------------------------- *
 * Display number (in range 0..99) as two decimal digits in full-screen
 * ------------------------------------------------------------------------- */
void
display_digits(uint8_t value);

/* ------------------------------------------------------------------------- *
 * Set border attributes
 * ------------------------------------------------------------------------- */
void
set_border(uint8_t border_attrs);

/* ------------------------------------------------------------------------- *
 * Set attributes of entire screen
 * ------------------------------------------------------------------------- */
#define set_screen_attrs(_ATTRS)  set_attrs(_ATTRS, 0, 0, 0x300)

/* ------------------------------------------------------------------------- *
 * Signal a fatal error message. Terminate all network activity, display
 * the message, and wait for the user to reset the machine.
 * ------------------------------------------------------------------------- */
void
fatal_error(uint8_t error_code);

#endif /* SPECCYBOOT_UTIL_INCLUSION_GUARD */

