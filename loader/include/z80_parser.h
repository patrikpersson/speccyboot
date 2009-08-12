/*
 * Module z80_parser:
 *
 * Accepts a stream of bytes, unpacks it as a Z80 snapshot, loads it into
 * RAM, and executes it.
 *
 * Also handles reading of raw data files.
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

/*
 * Notification callback:
 *
 * called when the a data file has been loaded. NOT called when snapshots
 * are loaded -- a context switch is then performed instead.
 */
#define NOTIFY_FILE_LOADED         notify_file_loaded

/*
 * Prototype for callback (the actual function name is #define'd in above).
 * The 'next_addr' argument points to the first byte *NOT* written to,
 * so the number of bytes can be deduced as 'next_addr - A' where A is the
 * address originally passed to z80_receive_data().
 */
void NOTIFY_FILE_LOADED(const void *next_addr);

/* -------------------------------------------------------------------------
 * Prepare for receiving a .z80 snapshot.
 * ------------------------------------------------------------------------- */
void
z80_expect_snapshot(void);

/* -------------------------------------------------------------------------
 * Prepare for receiving a data file. The data file will be loaded to
 * address 'dest', and at most 'maxsz' bytes will be accepted.
 * ------------------------------------------------------------------------- */
void
z80_expect_raw_data(void *dest, uint16_t maxsz);

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
