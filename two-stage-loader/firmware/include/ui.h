/*
 * Module ui:
 *
 * Access to ZX Spectrum display, keyboard, and sound.
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

#ifndef SPECCYBOOT_UI_INCLUSION_GUARD
#define SPECCYBOOT_UI_INCLUSION_GUARD

#include <stdint.h>

#pragma save
#pragma sdcc_hash +

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
#define PAPER(x)      ((x)*8)

#define ROW_LENGTH    (32)

/* ------------------------------------------------------------------------- */

#define BITMAP_BASE     (0x4000)
#define BITMAP_SIZE     (0x1800)
#define ATTRS_BASE      ((BITMAP_BASE)+(BITMAP_SIZE))
#define ATTRS_SIZE      (0x300)

#define ATTR_ADDRESS(_r, _c)     (ATTRS_BASE + (_r * 0x20) + _c)

/* Location of progress bar (bottom row) */
#define PROGRESS_BAR_BASE        ((ATTRS_BASE) + 0x2E0)

/* ------------------------------------------------------------------------- */

typedef char key_t;

#define KEY_NONE                0
#define KEY_ENTER               13
#define KEY_UP                  '7'
#define KEY_DOWN                '6'

/* -------------------------------------------------------------------------
 * Codes for fatal_error() and display_status(), displayed as border colours
 * ------------------------------------------------------------------------- */

#define STATUS_OK                 (BLACK)
#define STATUS_WAITING_FOR_DHCP   (BLUE)
#define STATUS_WAITING_FOR_TFTP   (GREEN)

#define FATAL_NO_RESPONSE         (RED)
#define FATAL_FILE_NOT_FOUND      (MAGENTA)
#define FATAL_INCOMPATIBLE        (CYAN)

/* -------------------------------------------------------------------------
 * Clear screen and set all attributes to INK 0, PAPER 0.
 * ------------------------------------------------------------------------- */
void
cls(void) __naked;

/* -------------------------------------------------------------------------
 * Display a string at given coordinates, in 8x8 font. The string is
 * terminated by the character 'terminator.'
 *
 * The displayed string is truncated or padded with spaces up to and
 * including the 'end_col' column.
 * ------------------------------------------------------------------------- */
void
print_at(uint8_t row,
	 uint8_t start_col,
	 uint8_t end_col,
	 char terminator,
	 const char *s)
__naked;

/* -------------------------------------------------------------------------
 * Wait for keypress. Handles repeat events.
 * ------------------------------------------------------------------------- */
key_t
wait_key(void);

/* -------------------------------------------------------------------------
 * Make a short sound, for a key click. This function will paint the border
 * black, regardless of its previous state. Interrupts will be enabled.
 * ------------------------------------------------------------------------- */
void
key_click(void)
__naked;

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col). If (col+n) extends
 * beyond the end of the row, changes will continue at the beginning of the
 * following row, for at most 256 cells in sequence.
 * ------------------------------------------------------------------------- */
void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint8_t n)
__naked;

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col). If (col+n) extends
 * beyond the end of the row, changes will continue at the beginning of the
 * following row. Only suitable for constant-valued arguments. Not limited to
 * 256 cells.
 * ------------------------------------------------------------------------- */

#define set_attrs_const(_attrs, _r, _c, _n)    __asm                          \
  ld  hl, #ATTR_ADDRESS((_r),(_c))					      \
  ld  de, #ATTR_ADDRESS((_r),(_c))+1					      \
  ld  bc, #(_n)-1  							      \
  ld  (hl), #(_attrs)							      \
  ldir                                                                        \
  __endasm

/* ------------------------------------------------------------------------- *
 * Display progress bar
 * ------------------------------------------------------------------------- */
void
display_progress(uint8_t kilobytes_loaded,
		 uint8_t kilobytes_expected);

/* ------------------------------------------------------------------------- *
 * Display status by border colour.
 * ------------------------------------------------------------------------- */
#define display_status   set_border

/* ------------------------------------------------------------------------- *
 * Signal a fatal error message. Terminate all network activity, set the
 * border to the indicated colour, and wait for the user to reset the machine.
 * ------------------------------------------------------------------------- */
#define fatal_error(_status)           {                                      \
  display_status(_status);                                                    \
  __asm                                                                       \
  di                                                                          \
  halt                                                                        \
  __endasm;                            }

/* ------------------------------------------------------------------------- *
 * Display a digit (for progress display)
 * ------------------------------------------------------------------------- */
#define display_digit_at(_digit, _row, _col)                                  \
  display_digit_impl(_digit, ATTR_ADDRESS(_row, _col))

void
display_digit_impl(uint8_t digit, uint8_t *start_address)
__naked;

/* ------------------------------------------------------------------------- *
 * Clear screen (black on black)
 * ------------------------------------------------------------------------- */
void
cls(void)
  __naked;

/* ------------------------------------------------------------------------- *
 * Set border attributes
 * ------------------------------------------------------------------------- */
sfr at(0xfe) _ula_port;

#define set_border(_clr)      _ula_port = (_clr) & 0x07

#pragma restore

#endif /* SPECCYBOOT_UI_INCLUSION_GUARD */

