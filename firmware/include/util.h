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

#ifndef SPECCYBOOT_UTIL_INCLUSION_GUARD
#define SPECCYBOOT_UTIL_INCLUSION_GUARD

#include <stdint.h>
#include <stddef.h>

#include "syslog.h"

/* -------------------------------------------------------------------------
 * MAC address
 * ------------------------------------------------------------------------- */

#ifndef MAC_ADDR_0

/*
 * These definitions can be overridden by passing new ones from Makefile
 * using -DMAC_ADDR_x=y
 *
 * (you need to do this to have more than one SpeccyBoot on the same LAN)
 *
 * NOTE: this is an LAA (Locally Administered Address), as signified by
 * bit 1 in MAC_ADDR_0.
 */

#define MAC_ADDR_0    (0xba)
#define MAC_ADDR_1    (0xdb)
#define MAC_ADDR_2    (0xad)
#define MAC_ADDR_3    (0xc0)
#define MAC_ADDR_4    (0xff)
#define MAC_ADDR_5    (0xee)

#endif

/* ------------------------------------------------------------------------- */

/*
 * Byteswapping/masking helpers
 */
#define HIBYTE(x)       (((uint16_t) (x)) >> 8)
#define LOBYTE(x)       (((uint16_t) (x)) & 0x00ffu)

#define BYTESWAP16(x)   (LOBYTE(x) * 0x0100 + HIBYTE(x))
#define htons(n)        BYTESWAP16(n)
#define ntohs           htons

#define BITS0TO7(x)     LOBYTE(x)
#define BITS8TO15(x)    (((x) >> 8) & 0x00ffu)
#define BITS16TO23(x)   (((x) >> 16) & 0x00ffu)
#define BITS24TO31(x)   (((x) >> 24) & 0x00ffu)

#define BYTESWAP32(x)   (  BITS0TO7(x)   * 0x01000000u                        \
                         + BITS8TO15(x)  * 0x00010000u                        \
                         + BITS16TO23(x) * 0x00000100u                        \
                         + BITS24TO31(x) )

#define htonl(n)        BYTESWAP32(n)
#define ntohl           htonl

/* ------------------------------------------------------------------------- */

/*
 * Packed structs
 */

#ifdef SDCC
/* SDCC packs structs by default */
#define PACKED_STRUCT(name)  struct name
#else
#error Need to configure packed structs for compiler!
#endif

/* -------------------------------------------------------------------------
 * Interrupt control
 * ------------------------------------------------------------------------- */

#define DISABLE_INTERRUPTS      __asm  di  _endasm
#define ENABLE_INTERRUPTS       __asm  ei  _endasm

/* -------------------------------------------------------------------------
 * Spectrum attributes
 * ------------------------------------------------------------------------- */

#define BLACK         (0x00)
#define BLUE          (0x01)
#define WHITE         (0x07)

#define BRIGHT        (0x40)
#define FLASH         (0x80)

#define INK(x)        (x)
#define PAPER(x)      ((x) << 3)

#define ROW_LENGTH    (32)

/* ------------------------------------------------------------------------- */

#define BITMAP_BASE     (0x4000)
#define BITMAP_SIZE     (0x1800)
#define ATTRS_BASE      ((BITMAP_BASE) + (BITMAP_SIZE))
#define ATTRS_SIZE      (0x300)

/* ------------------------------------------------------------------------- */

#define FONTDATA_ADDRESS(_ch)                                                 \
  (0x6F00 + (((uint16_t) (_ch)) << 3))

/* ------------------------------------------------------------------------- */

typedef char key_t;

#define KEY_NONE                0
#define KEY_ENTER               13
#define KEY_UP                  '7'
#define KEY_DOWN                '6'

/* ------------------------------------------------------------------------- */

/*
 * Default RAM bank (for a 16k/48k snapshot). Has to be even (non-contended)
 */
#define DEFAULT_BANK              (0)

/* ------------------------------------------------------------------------- */

#define TICKS_PER_SECOND          (50)

/* ------------------------------------------------------------------------- */

/*
 * Stack addresses (defined here so crt0.asm can find them via linker)
 */
uint8_t at(0x5b00) stack_bottom;
uint8_t at(0x5c00) stack_top;

/* ------------------------------------------------------------------------- */

/*
 * Type of a timer value
 */
typedef uint16_t timer_t;

/* -------------------------------------------------------------------------
 * Clear screen and set all attributes to INK 0, PAPER 0.
 *
 * Defined in crt0.asm.
 * ------------------------------------------------------------------------- */
void
cls(void);

/* -------------------------------------------------------------------------
 * Page in the indicated bank at 0xc000
 * ------------------------------------------------------------------------- */
#define select_bank(_bank_id)   _bank_selection = (_bank_id)

sfr banked at(0x7FFD) _bank_selection;

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col). If (col+n) extends
 * beyond the end of the row, changes will continue at the beginning of the
 * following row.
 * ------------------------------------------------------------------------- */
void
set_attrs(uint8_t screen_attrs,
          uint8_t row,
          uint8_t col,
          uint8_t n);

/* ------------------------------------------------------------------------- *
 * Set a single attribute byte
 * ------------------------------------------------------------------------- */
#define set_attr_byte(_attrs, _row, _col)                                     \
  *((uint8_t *) (ATTRS_BASE + ((_row) << 5) + (_col))) = (_attrs)

/* ------------------------------------------------------------------------- *
 * Display progress as kilobyte digits and a progress bar
 * ------------------------------------------------------------------------- */
void
display_progress(uint8_t kilobytes_loaded, uint8_t kilobytes_expected);

/* ------------------------------------------------------------------------- *
 * Set border attributes
 * ------------------------------------------------------------------------- */
sfr at(0xfe) _ula_port;

#define set_border(_clr)      _ula_port = (_clr) & 0x07

/* -------------------------------------------------------------------------
 * Load font data into RAM
 * ------------------------------------------------------------------------- */
void
load_font_data(void);

/* -------------------------------------------------------------------------
 * Display NUL-terminated string at given coordinates, in 8x8 font.
 * ------------------------------------------------------------------------- */
void
print_at(uint8_t line, uint8_t column, const char *s);

/* -------------------------------------------------------------------------
 * Display single char at given coordinates, in 8x8 font
 * ------------------------------------------------------------------------- */
void
print_char_at(uint8_t row, uint8_t column, char c);

/* -------------------------------------------------------------------------
 * Display 8x8 pattern at given location
 * ------------------------------------------------------------------------- */
void
print_pattern_at(uint8_t row, uint8_t col, const uint8_t *pattern);

/* -------------------------------------------------------------------------
 * Display 8x8 pattern at given location
 * ------------------------------------------------------------------------- */
void
print_pattern_with_attrs_at(uint8_t attrs,
                            uint8_t row,
                            uint8_t col,
                            const uint8_t *pattern);

/* -------------------------------------------------------------------------
 * Poll keyboard: return currently pressed key, or KEY_NONE
 *
 * Defined in crt0.asm
 * ------------------------------------------------------------------------- */
key_t
poll_key(void);

/* -------------------------------------------------------------------------
 * Wait for keypress. Handles repeat events.
 * ------------------------------------------------------------------------- */
key_t
wait_key(void);

/* -------------------------------------------------------------------------
 * Make a short sound, for a key click. This function will paint the border
 * black, regardless of its previous state.
 *
 * Defined in crt0.asm
 * ------------------------------------------------------------------------- */
void
key_click(void);

/* -------------------------------------------------------------------------
 * Reset/initialize a timer (set it to zero)
 * ------------------------------------------------------------------------- */
#define timer_reset(_tmr)                                                     \
  DISABLE_INTERRUPTS;                                                         \
  (_tmr) = timer_tick_count;                                                  \
  ENABLE_INTERRUPTS

extern volatile timer_t timer_tick_count;

/* -------------------------------------------------------------------------
 * Returns the value of the given timer, in ticks since it was reset
 * ------------------------------------------------------------------------- */
timer_t
timer_value(timer_t timer);

/* -------------------------------------------------------------------------
 * Wait for the next tick (vertical sync interrupt)
 * ------------------------------------------------------------------------- */
#define wait_for_tick()    __asm halt __endasm

/* ------------------------------------------------------------------------- *
 * Returns a pseudo-random byte value in the range 0..31. Taken from the R
 * register + a MAC address-dependent value.
 * ------------------------------------------------------------------------- */
uint8_t
rand5bits(void) __naked;

/* ------------------------------------------------------------------------- *
 * Signal a fatal error message. Terminate all network activity, display
 * the message, and wait for the user to reset the machine.
 * ------------------------------------------------------------------------- */
void
fatal_error(const char *message);

#endif /* SPECCYBOOT_UTIL_INCLUSION_GUARD */

