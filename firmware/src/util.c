/*
 *
 * Various low-level useful stuff.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
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

#include "util.h"

#include "eth.h"
#include "globals.h"
#include "syslog.h"

/* ------------------------------------------------------------------------- */

/*
 * Tick count, increased by the 50Hz timer ISR in crt0.asm
 */
volatile timer_t timer_tick_count = 0;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

timer_t
timer_value(timer_t timer)
{
  timer_t current_value;
  DISABLE_INTERRUPTS;
  current_value = timer_tick_count;
  ENABLE_INTERRUPTS;
  return current_value - timer;
}

/* ------------------------------------------------------------------------- */

#ifdef PAINT_STACK
void
paint_stack(void)
{
  __asm

    di
    pop   hl
    exx
    ld    hl, #0x5b00
    ld    de, #0x5b01
    ld    bc, #(STACK_SIZE - 1)
    ld    (hl), #STACK_MAGIC
    ldir
    ld    sp, #_stack_top
    ei
    exx
    jp    (hl)

  __endasm;
}
#endif
