/*
 * Module dhcp:
 *
 * Dynamic Host Configuration Protocol (DHCP, RFC 2131)
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

#include "dhcp.h"

#include "eth.h"
#include "globals.h"
#include "udp_ip.h"
#include "ui.h"
#include "tftp.h"

/* ========================================================================= */

/*
 * BOOTP operations, used by DHCP too
 */
#define BOOTREQUEST           (1)
#define BOOTREPLY             (2)

/*
 * DHCP message types
 */
#define DHCPDISCOVER          (1)
#define DHCPOFFER             (2)
#define DHCPREQUEST           (3)
#define DHCPACK               (5)

/*
 * DHCP options (RFC 2131)
 */
#define DHCP_OPTION_PAD         (0)
#define DHCP_OPTION_BCAST_ADDR  (28)
#define DHCP_OPTION_REQ_IP_ADDR (50)
#define DHCP_OPTION_MSG_TYPE    (53)
#define DHCP_OPTION_PARAM_REQ   (55)
#define DHCP_OPTION_CLIENTID    (61)
#define DHCP_OPTION_END         (255)

/* DHCP transaction ID; chosen as a constant (ASCII 'ZX82') for simplicity */
#define DHCP_XID                (0x5A583832)

/* DHCP magic, as specified in RFC2131 (99, 130, 83, 99 decimal) */
#define DHCP_MAGIC              (0x63825363)

/* ========================================================================= */

/*
 * DHCP{DISCOVER,REQUEST} packets, assembled as follows:
 *
 * DISCOVER
 * --------
 *
 *   udp_header
 * + dhcp_header
 * + dhcp_discover_options
 * + dhcp_common_options
 *
 * REQUEST
 * -------
 *
 *   udp_header
 * + dhcp_header
 * + dhcp_request_options_ipaddr
 * + (IP address received in DHCPOFFER)
 * + dhcp_common_options
 */

#define SIZEOF_DHCP_DISCOVER    ( sizeof(struct udp_header_t)                 \
                                + sizeof(struct dhcp_header_t)                \
                                + sizeof(dhcp_discover_options)               \
                                + sizeof(dhcp_common_options) )

#define SIZEOF_DHCP_REQUEST     ( sizeof(struct udp_header_t)                 \
                                + sizeof(struct dhcp_header_t)                \
                                + sizeof(dhcp_request_options_ipaddr)         \
                                + sizeof(ipv4_address_t)                      \
                                + sizeof(dhcp_common_options) )

static const struct dhcp_sub_header_t dhcp_sub_header = {
  BOOTREQUEST,                              /* dhcp_header.op */
  ETH_HWTYPE,                               /* dhcp_header.htype */
  sizeof(struct mac_address_t),             /* dhcp_header.hlen */
  0,                                        /* dhcp_header.hops */
  htonl(DHCP_XID),                          /* dhcp_header.xid */
  htons(10),                                /* dhcp_header.secs */
  0,                                        /* dhcp_header.flags */
  0x00000000,                               /* dhcp_header.ciaddr */
  0x00000000,                               /* dhcp_header.yiaddr */
  0x00000000,                               /* dhcp_header.siaddr */
  0x00000000,                               /* dhcp_header.giaddr */
  {{ MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2,
     MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5 }}  /* dhcp_header.chaddr */
};

static const uint32_t dhcp_magic = htonl(DHCP_MAGIC);

static const uint8_t dhcp_common_options[] = {
  DHCP_OPTION_PARAM_REQ,  1, DHCP_OPTION_BCAST_ADDR,
  DHCP_OPTION_END
};

static const uint8_t dhcp_discover_options[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPDISCOVER
};

static const uint8_t dhcp_request_options_ipaddr[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPREQUEST,
  DHCP_OPTION_REQ_IP_ADDR, 4      /* requested IP address follows */
};

/* ------------------------------------------------------------------------- */

/* Ensure assumptions about max Ethernet frame size is valid */

COMPILE_ASSERT(SIZEOF_DHCP_DISCOVER + sizeof(struct ipv4_header_t) <= ETH_MAX_TX_PAYLOAD);

COMPILE_ASSERT(SIZEOF_DHCP_REQUEST + sizeof(struct ipv4_header_t) <= ETH_MAX_TX_PAYLOAD);

/* ------------------------------------------------------------------------- */

/* Parameters received from DHCP, with default values */
static ipv4_address_t broadcast_address = IP_DEFAULT_BCAST_ADDRESS;
static uint8_t msg_type                 = DHCPACK;

/* ------------------------------------------------------------------------- */

/* Common beginning of all outgoing DHCP packets */
static void
dhcp_add_header(void)
{
  udp_add(dhcp_sub_header);

  /* DHCP setup is made with a clear screen: use VRAM as source of zeros */
  udp_add_w_len(BITMAP_BASE, DHCP_SIZEOF_ZEROS);

  udp_add(dhcp_magic);
}

/* ------------------------------------------------------------------------- */

/* Common end of all outgoing DHCP packets */
static void
dhcp_finalize_and_send(void)
{
  udp_add(dhcp_common_options);
  udp_send();
}

/* ------------------------------------------------------------------------- */

void
dhcp_init(void)
{
  display_status_configuring_dhcp();

  udp_create(&eth_broadcast_address,
	     &ip_config.broadcast_address,
	     htons(UDP_PORT_DHCP_CLIENT),
	     htons(UDP_PORT_DHCP_SERVER),
	     SIZEOF_DHCP_DISCOVER,
	     ETH_FRAME_PRIORITY);
  dhcp_add_header();
  udp_add(dhcp_discover_options);
  dhcp_finalize_and_send();
}

/* ------------------------------------------------------------------------- */

void
dhcp_receive(void)
{
  /*
   * Once an IP address is set, broadcasts are ignored. No need to check
   * for the BOUND state here.
   */

  if (rx_frame.udp.app.dhcp.header.sub.op != BOOTREPLY
      || rx_frame.udp.app.dhcp.header.sub.xid != htonl(DHCP_XID))
  {
    return;
  }

  /* Magic value indicates DHCP options present. */
  if (rx_frame.udp.app.dhcp.header.magic == dhcp_magic) {
    const uint8_t *options = rx_frame.udp.app.dhcp.options;

    for (;;) {
      uint8_t option = *options++;
      uint8_t option_length;
      
      if (option == DHCP_OPTION_PAD)  continue;
      if (option == DHCP_OPTION_END)  break;

      option_length = *options++;
      
      switch (option) {
      case DHCP_OPTION_BCAST_ADDR:
	broadcast_address = *((const ipv4_address_t *) options);
	break;
      case DHCP_OPTION_MSG_TYPE:
	msg_type = *options;
	break;
      }
      
      options += option_length;
    }
  }
    
  switch (msg_type) {
  case DHCPACK:
    ip_config.host_address      = rx_frame.udp.app.dhcp.header.sub.yiaddr;
    ip_config.broadcast_address = broadcast_address;
    
    display_status_configuring_tftp();
    tftp_read_request(SNAPSHOT_LIST_FILE);
    
    break;
        
  case DHCPOFFER:
    udp_create_reply(SIZEOF_DHCP_REQUEST);
    dhcp_add_header();
    udp_add(dhcp_request_options_ipaddr);
    udp_add(rx_frame.udp.app.dhcp.header.sub.yiaddr);
    dhcp_finalize_and_send();
        
    break;
  }
}
