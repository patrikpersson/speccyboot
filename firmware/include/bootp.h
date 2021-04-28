/*
 * Module bootp:
 *
 * Boot Protocol (BOOTP, RFC 951)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2021-  Patrik Persson
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
#ifndef SPECCYBOOT_BOOTP_INCLUSION_GUARD
#define SPECCYBOOT_BOOTP_INCLUSION_GUARD

/* -------------------------------------------------------------------------
 * Called by UDP when a BOOTP packet has been received.
 * If a BOOTREPLY with an IP address is found, call tftp_read_request().
 * ------------------------------------------------------------------------- */
void
bootp_receive(void);

/* -------------------------------------------------------------------------
 * Send a BOOTREQUEST for client configuration
 * (IP address, boot file name, TFTP server address)
 * ------------------------------------------------------------------------- */
void
bootp_init(void);

#endif /* SPECCYBOOT_BOOTP_INCLUSION_GUARD */
