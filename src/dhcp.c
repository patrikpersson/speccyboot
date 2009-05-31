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

#include "udp.h"
#include "dhcp.h"

#include "logging.h"

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
 * DHCP magic, as specified in RFC2131 (99, 130, 83, 99 decimal)
 */
#define DHCP_MAGIC              (0x63825363)

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
 * Requested lease time (one hour)
 */
#define DHCP_REQ_LEASE_TIME     (60 * 60)

/* =========================================================================
 * DHCP packets
 * ========================================================================= */

PACKED_STRUCT(dhcp_header_t) {            /* DHCP packet excluding options */
  uint8_t             op;
  uint8_t             htype;
  uint8_t             hlen;
  uint8_t             hops;
  uint32_t            xid;
  uint16_t            secs;
  uint16_t            flags;
  ipv4_address_t      ciaddr;
  ipv4_address_t      yiaddr;
  ipv4_address_t      siaddr;
  ipv4_address_t      giaddr;
  uint8_t             chaddr[16];
  char                sname[64];
  char                file[128];
  uint32_t            magic;              /* magic cookie for DHCP options */
};

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

#define SIZEOF_DHCP_DISCOVER    ( sizeof(dhcp_header)                         \
                                + sizeof(dhcp_discover_options)               \
                                + sizeof(dhcp_common_options) )

#define SIZEOF_DHCP_REQUEST     ( sizeof(dhcp_header)                         \
                                + sizeof(dhcp_request_options_ipaddr)         \
                                + sizeof(ipv4_address_t)                      \
                                + sizeof(dhcp_request_options_server)         \
                                + sizeof(ipv4_address_t)                      \
                                + sizeof(dhcp_common_options) )

const struct dhcp_header_t dhcp_header = {
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
  { MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2,
    MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5 },   /* dhcp_header.chaddr */
  { '\000' },                               /* dhcp_header.sname */
  { '\000' },                               /* dhcp_header.file */
  htonl(DHCP_MAGIC)                         /* dhcp_header.magic */
};

static const uint8_t dhcp_common_options[] = {
  DHCP_OPTION_PARAM_REQ,  1,          DHCP_OPTION_BCAST_ADDR,
  DHCP_OPTION_MAXSIZE,    2,          HIBYTE(DHCP_MAX_MSG_SIZE),
                                      LOBYTE(DHCP_MAX_MSG_SIZE),
  DHCP_OPTION_CLIENTID,   7,          0x01, MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2,
                                            MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5, 
  DHCP_OPTION_LEASE_TIME, 4,          0, 0, HIBYTE(DHCP_REQ_LEASE_TIME),
                                            LOBYTE(DHCP_REQ_LEASE_TIME),
  DHCP_OPTION_HOSTNAME,   8,          's', 'p', 'e', 'c', 't', 'r', 'u', 'm',
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

void
dhcp_init(void)
{
  dhcp_state = STATE_SELECTING;

  NOTIFY_DHCP_STATE(dhcp_state);

  udp_create_packet(&eth_broadcast_address,
                    &generic_broadcast_address,
                    htons(UDP_PORT_DHCP_CLIENT),
                    htons(UDP_PORT_DHCP_SERVER),
                    SIZEOF_DHCP_DISCOVER);
  udp_add_payload_to_packet(dhcp_header);
  udp_add_payload_to_packet(dhcp_discover_options);
  udp_add_payload_to_packet(dhcp_common_options);
  udp_send_packet(SIZEOF_DHCP_DISCOVER);
}

/* ------------------------------------------------------------------------- */

void
dhcp_packet_received(const ipv4_address_t        *src,
                     const struct dhcp_header_t  *packet)
{
  /*
   * DHCP server we are talking to: valid in REQUESTING state
   */
  static ipv4_address_t dhcp_server_address = 0x00000000;

  if (    (dhcp_state == STATE_INIT)
      ||  (dhcp_state == STATE_BOUND)
      || ((dhcp_state == STATE_REQUESTING)
             && (*src != dhcp_server_address)))
  {
    logging_add_entry("DHCP: ignoring packet", NULL);
    return;
  }
  
  if (packet->op == BOOTREPLY) {
    const uint8_t *options = ((const uint8_t *) packet)
                             + sizeof(struct dhcp_header_t);
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
    
    // logging_add_entry("DHCP: got message " DEC8_ARG, &msg_type);
    
    switch (msg_type) {
      case DHCPACK:
        if (dhcp_state != STATE_REQUESTING)  break;
        
        /* fall-through */

      case DHCP_NO_MSGTYPE:
        /*
         * No DHCP message type option found; assume BOOTP.
         * Take the parameters and consider the IP configuration done.
         */
        ip_config.host_address      = packet->yiaddr;
        ip_config.broadcast_address = broadcast_address;
        dhcp_state = STATE_BOUND;
        
        eth_reset_retransmission_timer();   /* Don't ask for more ACKs */
        
        NOTIFY_DHCP_STATE(dhcp_state);
        
        break;
        
      case DHCPOFFER:
        dhcp_server_address = *src;
        dhcp_state          = STATE_REQUESTING;

        NOTIFY_DHCP_STATE(dhcp_state);

        /*logging_add_entry("DHCP: offer for "
                          DEC8_ARG "." DEC8_ARG "." DEC8_ARG "." DEC8_ARG,
                          (uint8_t *) &packet->yiaddr);*/
        
        udp_create_packet(&eth_broadcast_address,
                          &generic_broadcast_address,
                          htons(UDP_PORT_DHCP_CLIENT),
                          htons(UDP_PORT_DHCP_SERVER),
                          SIZEOF_DHCP_REQUEST);
        udp_add_payload_to_packet(dhcp_header);
        udp_add_payload_to_packet(dhcp_request_options_ipaddr);
        udp_add_payload_to_packet(packet->yiaddr);
        udp_add_payload_to_packet(dhcp_request_options_server);
        udp_add_payload_to_packet(dhcp_server_address);
        udp_add_payload_to_packet(dhcp_common_options);
        udp_send_packet(SIZEOF_DHCP_REQUEST);
        
        break;
        
      default:
        logging_add_entry("DHCP: unexpected msgtype", NULL);
        break;
    }
  }
  else {
    logging_add_entry("DHCP: unexpected opcode", NULL);
  }
}
