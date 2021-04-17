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

#ifndef SPECCYBOOT_UI_INCLUSION_GUARD
#define SPECCYBOOT_UI_INCLUSION_GUARD

#include <stdint.h>

#include "udp_ip.h"

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

/* Location of local and server IP addresses (row 23, columns 6 and 22) */
#define LOCAL_IP_POS             (BITMAP_BASE + 0x1000 + 15*32 + 6)
#define SERVER_IP_POS            (BITMAP_BASE + 0x1000 + 15*32 + 22)

/* ------------------------------------------------------------------------- */

typedef char key_t;

#define KEY_NONE                0
#define KEY_ENTER               13
#define KEY_UP                  '7'
#define KEY_DOWN                '6'

/* -------------------------------------------------------------------------
 * Codes for fatal_error() displayed as border colours
 * ------------------------------------------------------------------------- */

#define FATAL_NO_RESPONSE           (RED)
#define FATAL_FILE_NOT_FOUND        (YELLOW)
#define FATAL_INCOMPATIBLE          (CYAN)
#define FATAL_INVALID_BOOT_SERVER   (MAGENTA)

/* -------------------------------------------------------------------------
 * Clear screen and set all attributes to INK 0, PAPER 0.
 * ------------------------------------------------------------------------- */
void
cls(void);

/* -------------------------------------------------------------------------
 * Display a string at given coordinates, in 8x8 font. The string is
 * terminated by the character 'terminator.'
 *
 * The displayed string is truncated or padded with spaces up to and
 * including column 31.
 * ------------------------------------------------------------------------- */
void
print_at(uint8_t row,
         uint8_t start_col,
         char terminator,
         const char *s);

/* -------------------------------------------------------------------------
 * Print IP address, in a slightly condensed font.
 * ------------------------------------------------------------------------- */
void
print_ip_addr(const ipv4_address_t *ip, uint8_t *vram_pos);

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
key_click(void);

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col).
 * ------------------------------------------------------------------------- */

#define set_attrs(_attrs, _r, _c, _n)                                         \
  set_attrs_impl(_attrs, (uint8_t *) ATTR_ADDRESS((_r),(_c)), _n)

void
set_attrs_impl(uint8_t attrs, uint8_t *attr_address, int len);

/* ------------------------------------------------------------------------- *
 * Display progress bar
 * ------------------------------------------------------------------------- */
void
display_progress(uint8_t kilobytes_loaded,
		 uint8_t kilobytes_expected);

/* ------------------------------------------------------------------------- *
 * Signal a fatal error message. Terminate all network activity, set the
 * border to the indicated colour, and wait for the user to reset the machine.
 * ------------------------------------------------------------------------- */
#define fatal_error(_status)           {                                      \
  set_border(_status);                                                        \
  __asm                                                                       \
  di                                                                          \
  halt                                                                        \
  __endasm;                            }

/* ------------------------------------------------------------------------- *
 * Display a digit (for progress display)
 * ------------------------------------------------------------------------- */
#define display_digit_at(_digit, _row, _col)                                  \
  display_digit_impl(_digit, (uint8_t *) ATTR_ADDRESS(_row, _col))

void
display_digit_impl(uint8_t digit, uint8_t *start_address);

/* ------------------------------------------------------------------------- *
 * Display the letter K (for progress display)
 * ------------------------------------------------------------------------- */
extern void
display_k(void);

/* ------------------------------------------------------------------------- *
 * Clear screen (black on black)
 * ------------------------------------------------------------------------- */
void
cls(void);

/* ------------------------------------------------------------------------- *
 * Set border attributes
 * ------------------------------------------------------------------------- */
__sfr __at(0xfe) _ula_port;

#define set_border(_clr)      _ula_port = (_clr) & 0x07

#pragma restore

#endif /* SPECCYBOOT_UI_INCLUSION_GUARD */
