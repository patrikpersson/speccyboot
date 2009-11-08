/*
 * Module dhcp:
 *
 * Dynamic Host Configuration Protocol (DHCP, RFC 2131)
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

#include <stddef.h>

#include "rxbuffer.h"

#include "udp_ip.h"
#include "arp.h"
#include "dhcp.h"

#include "syslog.h"

#include "eth.h"

/* ========================================================================= */

/*
 * BOOTP operations, used by DHCP too
 */
#define BOOTREQUEST           (1)
#define BOOTREPLY             (2)

/*
 * DHCP message types
 */
#define DHCP_NO_MSGTYPE       (0)
#define DHCPDISCOVER          (1)
#define DHCPOFFER             (2)
#define DHCPREQUEST           (3)
#define DHCPACK               (5)
#define DHCPNAK               (6)
#define DHCPRELEASE           (7)

/*
 * DHCP options (RFC 2131)
 */
#define DHCP_OPTION_PAD         (0)
#define DHCP_OPTION_HOSTNAME    (12)
#define DHCP_OPTION_BCAST_ADDR  (28)
#define DHCP_OPTION_REQ_IP_ADDR (50)
#define DHCP_OPTION_LEASE_TIME  (51)
#define DHCP_OPTION_MSG_TYPE    (53)
#define DHCP_OPTION_SERVER_ID   (54)
#define DHCP_OPTION_PARAM_REQ   (55)
#define DHCP_OPTION_MAXSIZE     (57)
#define DHCP_OPTION_CLIENTID    (61)
#define DHCP_OPTION_END         (255)

/*
 * DHCP transaction ID; chosen as a constant (ASCII 'ZX82') for simplicity
 * 
 */
#define DHCP_XID                (0x5A583832)

/*
 * Maximal DHCP message size accepted by this client (576 is the minimal
 * legal value according to RFC2132)
 */
#define DHCP_MAX_MSG_SIZE       (576)

/*
 * Requested lease time (12 hours)
 */
#define DHCP_REQ_LEASE_TIME     (43200)

/* ========================================================================= */

/*
 * DHCP{DISCOVER,REQUEST} packets, assembled as follows:
 *
 * DISCOVER
 * --------
 *
 *   dhcp_header
 * + dhcp_discover_options
 * + dhcp_common_options
 *
 * REQUEST
 * -------
 *
 * dhcp_header
 * + dhcp_request_options_ipaddr
 * + (IP address received in DHCPOFFER)
 * + dhcp_request_options_server
 * + (sender of DHCPOFFER)
 * + dhcp_common_options
 */

#define SIZEOF_DHCP_DISCOVER    ( sizeof(struct dhcp_header_t)                \
                                + sizeof(dhcp_discover_options)               \
                                + sizeof(dhcp_common_options) )

#define SIZEOF_DHCP_REQUEST     ( sizeof(struct dhcp_header_t)                \
                                + sizeof(dhcp_request_options_ipaddr)         \
                                + sizeof(ipv4_address_t)                      \
                                + sizeof(dhcp_request_options_server)         \
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

/*
 * DHCP magic, as specified in RFC2131 (99, 130, 83, 99 decimal)
 */
static const uint32_t dhcp_magic = htonl(0x63825363);

static const uint8_t dhcp_common_options[] = {
  DHCP_OPTION_PARAM_REQ,  1,          DHCP_OPTION_BCAST_ADDR,
  DHCP_OPTION_MAXSIZE,    2,          HIBYTE(DHCP_MAX_MSG_SIZE),
                                      LOBYTE(DHCP_MAX_MSG_SIZE),
  DHCP_OPTION_CLIENTID,   7,          0x01, MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2,
                                            MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5, 
  DHCP_OPTION_LEASE_TIME, 4,          0, 0, HIBYTE(DHCP_REQ_LEASE_TIME),
                                            LOBYTE(DHCP_REQ_LEASE_TIME),
  DHCP_OPTION_HOSTNAME,   6,          's', 'p', 'e', 'c', 'c', 'y',
  DHCP_OPTION_END
};

static const uint8_t dhcp_discover_options[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPDISCOVER,
};

static const uint8_t dhcp_request_options_ipaddr[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPREQUEST,
  DHCP_OPTION_REQ_IP_ADDR, 4      /* requested IP address follows */
};

static const uint8_t dhcp_request_options_server[] = {
  DHCP_OPTION_SERVER_ID, 4        /* server IP address follows */
};

/* ------------------------------------------------------------------------- */

/*
 * Broadcast address to use for DHCP DISCOVER/REQUEST
 */
static const ipv4_address_t generic_broadcast_address
  = IP_DEFAULT_BCAST_ADDRESS;

/* ------------------------------------------------------------------------- */

static enum dhcp_state_t dhcp_state = STATE_INIT;

/* ------------------------------------------------------------------------- */

static void
dhcp_send_header(void)
{
  uint8_t i;
  udp_add_payload_to_packet(dhcp_sub_header);
  for (i = 0; i < DHCP_SIZEOF_ZEROS; i++) {
    udp_add_payload_byte_to_packet(0);
  }
  udp_add_payload_to_packet(dhcp_magic);
}

/* ------------------------------------------------------------------------- */

void
dhcp_init(void)
{
  udp_create_packet(&eth_broadcast_address,
                    &generic_broadcast_address,
                    htons(UDP_PORT_DHCP_CLIENT),
                    htons(UDP_PORT_DHCP_SERVER),
                    SIZEOF_DHCP_DISCOVER,
                    ETH_FRAME_PRIORITY);
  dhcp_send_header();
  udp_add_payload_to_packet(dhcp_discover_options);
  udp_add_payload_to_packet(dhcp_common_options);
  udp_send_packet();
}

/* ------------------------------------------------------------------------- */

void
dhcp_packet_received(void)
{
  /*
   * DHCP server we are talking to: valid in REQUESTING state
   */
  static ipv4_address_t dhcp_server_address;

  if (    (dhcp_state == STATE_BOUND)
      || ((dhcp_state == STATE_REQUESTING)
          && (rx_frame.ip.src_addr != dhcp_server_address)))
  {
    return;
  }
  
  if (rx_frame.udp.app.dhcp.header.sub.op == BOOTREPLY) {
    const uint8_t *options           = rx_frame.udp.app.dhcp.options;
    ipv4_address_t broadcast_address = IP_DEFAULT_BCAST_ADDRESS;
    uint8_t msg_type                 = DHCP_NO_MSGTYPE;

    /*
     * Parse DHCP options, extract message type and broadcast address,
     * ignore everything else
     */
    for (;;) {
      uint8_t option = *options++;
      uint8_t option_length;

      if (option == DHCP_OPTION_PAD)  continue;
      if (option == DHCP_OPTION_END)  break;

      option_length = *options++;
      
      switch (option) {
        case DHCP_OPTION_BCAST_ADDR:
          {
            const ipv4_address_t *addr_p = (const ipv4_address_t *) options;
            broadcast_address            = *addr_p;
          }
          break;
        case DHCP_OPTION_MSG_TYPE:
          msg_type = *options;
          break;
      }
      
      options += option_length;
    }
    
    switch (msg_type) {
      case DHCPACK:
        if (dhcp_state != STATE_REQUESTING)  break;
        
        /* else fall-through */

      case DHCP_NO_MSGTYPE:
        /*
         * No DHCP message type option found; assume BOOTP.
         * Take the parameters and consider the IP configuration done.
         */
        ip_config.host_address      = rx_frame.udp.app.dhcp.header.sub.yiaddr;
        ip_config.broadcast_address = broadcast_address;
        dhcp_state = STATE_BOUND;
        
        NOTIFY_DHCP_BOUND();
        
        break;
        
      case DHCPOFFER:
        dhcp_server_address = rx_frame.ip.src_addr;
        dhcp_state          = STATE_REQUESTING;
        
        udp_create_reply(SIZEOF_DHCP_REQUEST);
        dhcp_send_header();
        udp_add_payload_to_packet(dhcp_request_options_ipaddr);
        udp_add_payload_to_packet(rx_frame.udp.app.dhcp.header.sub.yiaddr);
        udp_add_payload_to_packet(dhcp_request_options_server);
        udp_add_payload_to_packet(dhcp_server_address);
        udp_add_payload_to_packet(dhcp_common_options);
        udp_send_packet();
        
        break;
        
      default:
        break;
    }
  }
}
