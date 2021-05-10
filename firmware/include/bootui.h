/*
 * Module bootui:
 *
 * Rudimentary UI for the initial boot process.
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

#ifndef SPECCYBOOT_BOOTUI_INCLUSION_GUARD
#define SPECCYBOOT_BOOTUI_INCLUSION_GUARD

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

/* -------------------------------------------------------------------------
 * Codes for fatal_error() displayed as border colours
 * ------------------------------------------------------------------------- */

#define FATAL_NO_RESPONSE           (RED)
#define FATAL_FILE_NOT_FOUND        (YELLOW)
#define FATAL_INCOMPATIBLE          (CYAN)
#define FATAL_INVALID_BOOT_SERVER   (MAGENTA)
#define FATAL_INTERNAL_ERROR        (WHITE)

#define ULA_PORT                    (0xFE)

/* -------------------------------------------------------------------------
 * Set border to the value of register A, and halt the machine (DI+HALT)
 * ------------------------------------------------------------------------- */
void
fail(void);

#define fatal_error(_status)                                                  \
  __asm                                                                       \
    ld   a, #(_status)                                                        \
    jp   _fail                                                                \
  __endasm

#pragma restore

#endif /* SPECCYBOOT_BOOTUI_INCLUSION_GUARD */
