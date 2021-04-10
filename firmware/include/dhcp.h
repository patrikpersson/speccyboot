/*
 * Module dhcp:
 *
 * Dynamic Host Configuration Protocol (DHCP, RFCs 2131, 2132, 5859)
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
#ifndef SPECCYBOOT_DHCP_INCLUSION_GUARD
#define SPECCYBOOT_DHCP_INCLUSION_GUARD

#include "eth.h"
#include "udp_ip.h"

/* =========================================================================
 * DHCP packets
 * ========================================================================= */

/*
 * DHCP header excluding server and file names (always zero in outgoing
 * packets) and options.
 *
 * The reason for breaking the header into this subheader and "the rest" is
 * that we need to define a constant value of dhcp_sub_header_t, and using
 * dhcp_header_t for that value would result in 200+ zero bytes (waste of
 * space).
 */
PACKED_STRUCT(dhcp_sub_header_t) {
  uint8_t                   op;
  uint8_t                   htype;
  uint8_t                   hlen;
  uint8_t                   hops;
  uint32_t                  xid;
  uint16_t                  secs;
  uint16_t                  flags;
  ipv4_address_t            ciaddr;
  ipv4_address_t            yiaddr;
  ipv4_address_t            siaddr;
  ipv4_address_t            giaddr;
  struct mac_address_t      chaddr;
};

/* ------------------------------------------------------------------------- */

#define DHCP_SIZEOF_HWADDR_PADDING   (16 - sizeof(struct mac_address_t))
#define DHCP_SIZEOF_SNAME            (64)
#define DHCP_SIZEOF_FILE             (128)

#define DHCP_SIZEOF_TOTAL            (576)

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(dhcp_header_t) {              /* DHCP packet excluding options */
  struct dhcp_sub_header_t  sub;
  uint8_t                   hwaddr_padding[DHCP_SIZEOF_HWADDR_PADDING];
  uint8_t                   sname[DHCP_SIZEOF_SNAME];
  uint8_t                   file[DHCP_SIZEOF_FILE];
  uint32_t                  magic;          /* magic cookie for DHCP options */
};

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(dhcp_packet_t) {
  struct dhcp_header_t header;
  uint8_t              options[DHCP_SIZEOF_TOTAL
                               - sizeof(struct dhcp_header_t)];
};

/* -------------------------------------------------------------------------
 * Called by UDP when a DHCP packet has been received
 * ------------------------------------------------------------------------- */
void
dhcp_receive(void);

/* -------------------------------------------------------------------------
 * Obtain an IP address using DHCP. When an address has been obtained, call
 * tftp_read_request().
 * ------------------------------------------------------------------------- */
void
dhcp_init(void);

#endif /* SPECCYBOOT_DHCP_INCLUSION_GUARD */
