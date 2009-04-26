/*
 * Module spectrum:
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

#ifndef SPECCYBOOT_SPECTRUM_INCLUSION_GUARD
#define SPECCYBOOT_SPECTRUM_INCLUSION_GUARD

#include <stdint.h>

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
 * String constants used by spectrum_print_at
 *
 * HEX16 refers to a little-endian 16-bit number stored in two consecutive
 * uint8_t's in the array. A big endian 16-bit number would be represented
 * as two HEX8_ARGs.
 * ------------------------------------------------------------------------- */
#define HEX8_ARG         "\001"
#define HEX8_ARG_CHAR    '\001'
#define HEX16_ARG        "\002"
#define HEX16_ARG_CHAR   '\002'
#define DEC8_ARG         "\003"
#define DEC8_ARG_CHAR    '\003'
#define BOLD_ON          "\004"
#define BOLD_ON_CHAR     '\004'
#define BOLD_OFF         "\005"
#define BOLD_OFF_CHAR    '\005'

/* ------------------------------------------------------------------------- */

/*
 * Byteswapping/masking helpers
 */
#define HIBYTE(x)       ((x & 0xff00u) ? ((x) >> 8) : 0)
#define LOBYTE(x)       ((x) & 0x00ffu)

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

/* ------------------------------------------------------------------------- */

/*
 * Take a pointer, assume it points to a function, and call it.
 *
 * This thing is actually more efficient than inline assembly --
 * the compiler knows about the jump, so if JUMP_TO is used at the end of a
 * function, it results in slightly more efficient code (JP instead of
 * CALL + RET).
 */
#define JUMP_TO(ptr) ((void (*)(void)) (ptr))()

/* -------------------------------------------------------------------------
 * Macros for Z80 I/O ports
 * ------------------------------------------------------------------------- */

#define Z80_PORT(addr)                   sfr banked at (addr)
#define Z80_PORT_WRITE(port, value)      port = value
#define Z80_PORT_READ(port)              (port)

/* -------------------------------------------------------------------------
 * Possible key/joystick input.
 * ------------------------------------------------------------------------- */
enum spectrum_input_t {
  INPUT_NONE  = 0,    /* no key */
  INPUT_FIRE  = 1,    /* space/kempston fire/0 */
  INPUT_DOWN  = 2,    /* 6/kempston down */
  INPUT_UP    = 3     /* 7/kempston up */
};

/* -------------------------------------------------------------------------
 * Display splash screen
 * ------------------------------------------------------------------------- */
void
display_splash(void);

/* -------------------------------------------------------------------------
 * Copy font from Sinclair ROM to RAM.
 * ------------------------------------------------------------------------- */
void
spectrum_init_font(void);

/* -------------------------------------------------------------------------
 * Clear screen using given attributes, e.g.,
 *
 *   spectrum_cls(INK(RED) | PAPER(BLUE) | BRIGHT, GREEN)
 * ------------------------------------------------------------------------- */
void
spectrum_cls(const uint8_t screen_attrs, const uint8_t border_attrs);

/* ------------------------------------------------------------------------- *
 * Set attributes for n elements, starting at (row, col). If (col+n) extends
 * beyond the end of the row, changes will continue at the beginning of the
 * following row.
 * ------------------------------------------------------------------------- */
void
spectrum_set_attrs(const uint8_t screen_attrs,
                   const uint8_t row,
                   const uint8_t col,
                   const uint16_t n);

/* ------------------------------------------------------------------------- *
 * Display text string at given position on screen. The string is terminated
 * by NUL. Row is in range 0..23, col is in range 0..31.
 *
 * Occurrences of the char DEC_ARG/HEX_ARG in str will be replaced by the
 * corresponding byte in args in dec/hex. These numbers are always interpreted
 * as 8 bits unsigned.
 * ------------------------------------------------------------------------- */
void
spectrum_print_at(uint8_t row,
                  uint8_t col, 
                  const char *str,
                  const uint8_t *args);

/* ------------------------------------------------------------------------- *
 * ------------------------------------------------------------------------- */
void
spectrum_print_big_at(uint8_t row,
                      uint8_t col, 
                      const char *str);

/* ------------------------------------------------------------------------- *
 * Scroll all lines up on screen, leaving an empty line at the bottom.
 * Attributes are not affected, only the bitmap.
 * ------------------------------------------------------------------------- */
void
spectrum_scroll(void);

/* ------------------------------------------------------------------------- *
 * Returns current key pressed. Always returns immediately. If no key is
 * currently pressed, return INPUT_NONE.
 * ------------------------------------------------------------------------- */
enum spectrum_input_t
spectrum_poll_input(void);

/* ------------------------------------------------------------------------- *
 * Wait for a key to be pressed, then return the identity of that key.
 *
 * If a key is currently pressed, first waits for that to be released.
 * ------------------------------------------------------------------------- */
enum spectrum_input_t
spectrum_wait_input(void);

/* ------------------------------------------------------------------------- *
 * Signal a fatal error message. Terminate all network activity, display
 * the message, and wait for the user to reset the machine.
 * ------------------------------------------------------------------------- */
void
fatal_error(const char *message);

#endif /* SPECCYBOOT_SPECTRUM_INCLUSION_GUARD */

