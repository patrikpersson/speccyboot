/*
 * Module z80_parser:
 *
 * Accepts a stream of bytes, unpacks it as a Z80 snapshot, loads it into
 * RAM, and executes it.
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

#ifndef SPECCYBOOT_Z80_PARSER_INCLUSION_GUARD
#define SPECCYBOOT_Z80_PARSER_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Called from tftp.c when a TFTP DATA datagram has been received.
 *
 * data:                pointer to received data
 * nbr_bytes_data:      number of bytes in the buffer pointed to by 'data'
 * more_data_expected:  true if more data packets will arrive, false if all
 *                      data in the file has been transferred
 * ------------------------------------------------------------------------- */
void
z80_receive_data(const uint8_t *data,
                 uint16_t       nbr_bytes_data,
                 bool           more_data_expected);

#endif /* SPECCYBOOT_Z80_PARSER_INCLUSION_GUARD */