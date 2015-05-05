/*
 * Module tftp:
 *
 * Trivial File Transfer Protocol (TFTP, RFC 1350)
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
#ifndef SPECCYBOOT_TFTP_INCLUSION_GUARD
#define SPECCYBOOT_TFTP_INCLUSION_GUARD

#include "udp_ip.h"

/* ------------------------------------------------------------------------- */

#define SNAPSHOT_LIST_FILE  "snapshots.lst"

/* ------------------------------------------------------------------------- */

/*
 * TFTP DATA packets have a maximal size of 512 bytes, unless options are set
 * by the client (and this client won't)
 */
#define TFTP_DATA_MAXSIZE         (512)

/* Buffer where received TFTP data is loaded */
#define TFTP_DATA_BUFFER          (rx_frame.udp.app.tftp.data.raw_bytes)

/* =========================================================================
 * TFTP packets
 * ========================================================================= */

PACKED_STRUCT(tftp_header_t) {
  uint16_t        opcode;
  uint16_t        block_no;
};

/* -------------------------------------------------------------------------
 * Called by UDP when a TFTP packet has been identified
 * ------------------------------------------------------------------------- */
void
tftp_receive(void);

/* -------------------------------------------------------------------------
 * Initiate a file transfer from server.
 * ------------------------------------------------------------------------- */
void
tftp_read_request(const char *filename);

#endif /* SPECCYBOOT_TFTP_INCLUSION_GUARD */
