/*
 * Module tftp:
 *
 * Trivial File Transfer Protocol (TFTP, RFC 1350)
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
#ifndef SPECCYBOOT_TFTP_INCLUSION_GUARD
#define SPECCYBOOT_TFTP_INCLUSION_GUARD

#include "udp.h"

/* ------------------------------------------------------------------------- */

/*
 * Notification callback:
 *
 * called when data has been received over TFTP.
 */
#define NOTIFY_TFTP_DATA       z80_receive_data

/*
 * Prototype for callback (the actual function name is #define'd in above)
 *
 * received_data:           points to a buffer holding the received data
 * nbr_of_bytes_received:   number of valid bytes in buffer (possibly zero)
 * more_data_expected:      true if more packets are expected, false if this
 *                          was the last data packet
 */
void NOTIFY_TFTP_DATA(const uint8_t *received_data,
                      uint16_t       nbr_of_bytes_received,
                      bool           more_data_expected);

/* ------------------------------------------------------------------------- */

union tftp_packet_t;

/* -------------------------------------------------------------------------
 * Called by UDP when a TFTP packet has been identified
 * ------------------------------------------------------------------------- */
void
tftp_packet_received(const struct mac_address_t  *src_hwaddr,
                     const ipv4_address_t        *src,
                     uint16_t                     src_port_nw_order,
                     const union tftp_packet_t   *packet,
                     uint16_t                     nbr_bytes_in_packet);

/* -------------------------------------------------------------------------
 * Initiate a file transfer from server
 * ------------------------------------------------------------------------- */
void
tftp_read_request(const char *filename);

#endif /* SPECCYBOOT_TFTP_INCLUSION_GUARD */
