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
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ZEB_SPECTRUM_HW_INCLUSION_GUARD
#define ZEB_SPECTRUM_HW_INCLUSION_GUARD

#include <stdint.h>

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

/* ------------------------------------------------------------------------- */

/*
 * Take a pointer, assume it points to a function, and call it.
 *
 * This thing is actually more efficient than inline assembly --
 * the compiler knows about the jump, so it generates slightly more
 * efficient code (skips the final RET in the caller).
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
  INPUT_FIRE  = 1,    /* space/kempston fire/0 */
  INPUT_DOWN  = 2,    /* 6/kempston down */
  INPUT_UP    = 3     /* 7/kempston up */
};

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
 * ------------------------------------------------------------------------- */
void
spectrum_print_at(uint8_t row, uint8_t col, const char *str);

/* ------------------------------------------------------------------------- *
 * Scroll all lines up on screen, leaving an empty line at the bottom.
 * Attributes are not affected, only the bitmap.
 * ------------------------------------------------------------------------- */
void
spectrum_scroll(void);

/* ------------------------------------------------------------------------- *
 * Wait for a key to be pressed, then return the identity of that key.
 *
 * If a key is currently pressed, first waits for that to be released.
 * ------------------------------------------------------------------------- */

enum spectrum_key_t
spectrum_wait_input(void);

#endif /* ZEB_SPECTRUM_HW_INCLUSION_GUARD */

