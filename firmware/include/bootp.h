/*
 * Module bootp:
 *
 * Boot Protocol (BOOTP, RFC 951)
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
#ifndef SPECCYBOOT_BOOTP_INCLUSION_GUARD
#define SPECCYBOOT_BOOTP_INCLUSION_GUARD

#include "eth.h"
#include "udp_ip.h"

/* =========================================================================
 * BOOTP packets
 * ========================================================================= */

PACKED_STRUCT(bootp_header_t) {
  uint8_t                   op;
  uint8_t                   htype;
  uint8_t                   hlen;
  uint8_t                   hops;
  uint32_t                  xid;
};

/* These are all zero in the BOOTREQUEST */
PACKED_STRUCT(bootp_part1_t) {
  uint16_t                  secs;
  uint16_t                  unused;
  ipv4_address_t            ciaddr;
  ipv4_address_t            yiaddr;
  ipv4_address_t            siaddr;
  ipv4_address_t            giaddr;
};

/* These are all zero in the BOOTREQUEST */
PACKED_STRUCT(bootp_part2_t) {
  uint8_t                   chaddr_padding[10];
  uint8_t                   sname[64];
  uint8_t                   file[128];
  uint8_t                   vend[64];
};

PACKED_STRUCT(bootp_packet_t) {
  struct bootp_header_t     header;
  struct bootp_part1_t      part1;
  struct mac_address_t      chaddr;
  struct bootp_part2_t      part2;
};

/* -------------------------------------------------------------------------
 * Called by UDP when a BOOTP packet has been received
 * ------------------------------------------------------------------------- */
void
bootp_receive(void);

/* -------------------------------------------------------------------------
 * Obtain client configuration (IP address, boot file name, TFTP server
 * address) using BOOTP.
 * When an address has been obtained, call tftp_read_request().
 * ------------------------------------------------------------------------- */
void
bootp_init(void);

#endif /* SPECCYBOOT_BOOTP_INCLUSION_GUARD */
