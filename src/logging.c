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

void
logging_add_entry(const char *msg, const uint8_t *args)
{
  spectrum_scroll();
  spectrum_print_at(22, 0, msg, args);
  
  /*
   * Pause while a key is being pressed
   */
#if 0
  if (spectrum_poll_input() == INPUT_FIRE) {
    static Z80_PORT(254) border;
    border = 7;
    while (spectrum_poll_input() != INPUT_NONE)
      ;
    border = 3;
    spectrum_wait_input();
    border = 5;
    while (spectrum_poll_input() != INPUT_NONE)
      ;
    border = 0;
  }
#endif
}
