/*
 * Module z80_loader:
 *
 * Accepts a stream of bytes, unpacks it as a .z80 snapshot,
 * loads it into RAM, and executes it.
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

#ifndef SPECCYBOOT_Z80_LOADER_INCLUSION_GUARD
#define SPECCYBOOT_Z80_LOADER_INCLUSION_GUARD

/* -------------------------------------------------------------------------
 * Called from tftp.c to indicate that a .z80 snapshot is expected.
 * Installs a TFTP read hook to relay received data to Z80 snapshot parser.
 * ------------------------------------------------------------------------- */
void
expect_snapshot(void);

#endif /* SPECCYBOOT_Z80_LOADER_INCLUSION_GUARD */