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

#include <stdarg.h>

#include "spectrum.h"
#include "logging.h"

/* ------------------------------------------------------------------------- */

void
logging_init(void)
{
  spectrum_cls(INK(GREEN) | PAPER(BLACK), BLACK);
  spectrum_set_attrs(INK(GREEN) | PAPER(BLACK) | BRIGHT, 23, 0, ROW_LENGTH);
}

/* ------------------------------------------------------------------------- */

#define hexdigit(n) ( ((n) < 10) ? ((n) + '0') : ((n) + 'a' - 10))

void
logging_add_entry(const char *msg, ...)
{
  static char buf[ROW_LENGTH + 1];
  int buf_idx = 0;
  va_list args;
  
  va_start(args, msg);
  while (buf_idx < ROW_LENGTH) {
    char c = *msg++;

    if (c == '\0') break;
    
    if ((c & 0x80) != 0) {
      uint8_t byte = va_arg(args, uint8_t);

      buf[buf_idx++] = hexdigit(byte >> 4);
      if (buf_idx < ROW_LENGTH) {
        buf[buf_idx++] = hexdigit(byte & 0x0f);
      }
    }
    else {
      buf[buf_idx++] = c;
    }
  }
  va_end(args);
  buf[buf_idx] = '\0';

  spectrum_scroll();
  spectrum_print_at(23, 0, buf);
}
