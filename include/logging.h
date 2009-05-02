/*
 * Module logging:
 *
 * Diagnostic output, line-by-line.
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

#ifndef SPECCYBOOT_LOGGING_INCLUSION_GUARD
#define SPECCYBOOT_LOGGING_INCLUSION_GUARD

#include <stdint.h>

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

#ifdef VERBOSE_LOGGING
/* -------------------------------------------------------------------------
 * Initialize logging: clear screen, show cursor
 * ------------------------------------------------------------------------- */
void
logging_init(void);

/* -------------------------------------------------------------------------
 * Scroll everything one line up, and add a new entry at the bottom.
 * Arguments work like spectrum_print_at().
 * ------------------------------------------------------------------------- */
void
logging_add_entry(const char *msg, const uint8_t *args);

#else
/* VERBOSE_LOGGING */

#define logging_init()
#define logging_add_entry(msg, args)    ((void) msg, args)

#endif
/* VERBOSE_LOGGING */

#endif /* SPECCYBOOT_LOGGING_INCLUSION_GUARD */
