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

#include "bootp.h"

#include "eth.h"
#include "file_loader.h"
#include "globals.h"
#include "udp_ip.h"
#include "ui.h"
#include "syslog.h"
#include "tftp.h"

/* ========================================================================= */

/* BOOTP operations */
#define BOOTREQUEST           (1)
#define BOOTREPLY             (2)

/* BOOTP transaction ID; chosen as a constant (ASCII 'ZX82') for simplicity */
#define BOOTP_XID             (0x5A583832)

/*
 * BOOTP configuration is done with a clear screen,
 * so we can use video memory as a source of zeros
 * (useful for padding packets)
 */
#define ADDRESS_OF_ZEROS      (const uint8_t *) (BITMAP_BASE)

/* ------------------------------------------------------------------------- */

static const struct bootp_header_t bootrequest_header = {
  BOOTREQUEST,                    /* op */
  1,                              /* htype (10mbps Ethernet) */
  sizeof(struct mac_address_t),   /* hlen */
  0,                              /* hops */
  htonl(BOOTP_XID)                /* xid */
};

/* ------------------------------------------------------------------------- */

void
bootp_init(void)
{
  udp_create(&eth_broadcast_address,
             &ip_bcast_address,
             htons(UDP_PORT_BOOTP_CLIENT),
             htons(UDP_PORT_BOOTP_SERVER),
             sizeof(struct udp_header_t) + sizeof(struct bootp_packet_t),
             ETH_FRAME_PRIORITY);
  udp_add(bootrequest_header);
  udp_add_w_len(ADDRESS_OF_ZEROS, sizeof(struct bootp_part1_t));
  udp_add(eth_local_address);
  udp_add_w_len(ADDRESS_OF_ZEROS, sizeof(struct bootp_part2_t));
  udp_send();
}

/* ------------------------------------------------------------------------- */

void
bootp_receive(void)
{
  if (rx_frame.udp.app.bootp.header.op != BOOTREPLY
      || rx_frame.udp.app.bootp.header.xid != htonl(BOOTP_XID))
  {
    return;
  }

  ip_config.host_address = rx_frame.udp.app.bootp.part1.yiaddr;

  uint8_t *ip_addr = (uint8_t *) &ip_config.tftp_server_address;
  const uint8_t *p = rx_frame.udp.app.bootp.part2.sname;

  for (int i = 0; i < 4; i++) {
    uint8_t oct = (*p++) - '0';
    if (*p >= '0' && *p <= '9') {
      oct = (oct << 3) + (oct << 1) + (*p++) - '0';
      if (*p >= '0' && *p <= '9') {
        oct = (oct << 3) + (oct << 1) + (*p++) - '0';
      }
    }
    *ip_addr++ = oct;
    if (i < 3 && *p != '.') {
      fatal_error(FATAL_INVALID_BOOT_SERVER);
    }
    p++;
  }

  tftp_read_request(rx_frame.udp.app.bootp.part2.file);
}
