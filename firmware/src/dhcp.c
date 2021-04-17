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

#include "dhcp.h"

#include "eth.h"
#include "file_loader.h"
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
#define DHCP_OPTION_PAD               (0)
#define DHCP_OPTION_BCAST_ADDR        (28)
#define DHCP_OPTION_REQ_IP_ADDR       (50)
#define DHCP_OPTION_OVERLOAD          (52)
#define DHCP_OPTION_MSG_TYPE          (53)
#define DHCP_OPTION_SERVER_ID         (54)
#define DHCP_OPTION_PARAM_REQ         (55)
#define DHCP_OPTION_CLIENTID          (61)
#define DHCP_OPTION_TFTP_SERVER_NAME  (66)
#define DHCP_OPTION_BOOTFILE          (67)
#define DHCP_OPTION_TFTP_SERVER_ADDR  (150)
#define DHCP_OPTION_END               (255)

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
                                + sizeof(dhcp_request_options_server)         \
                                + sizeof(ipv4_address_t)                      \
                                + sizeof(dhcp_common_options) )

/*
 * DHCP configuration is done with a clear screen,
 * so we can use video memory as a source of zeros
 * (useful for padding packets)
 */
#define ADDRESS_OF_ZEROS        (const uint8_t *) (BITMAP_BASE)

static const struct dhcp_sub_header_t dhcp_sub_header = {
  BOOTREQUEST,                              /* dhcp_header.op */
  ETH_HWTYPE,                               /* dhcp_header.htype */
  sizeof(struct mac_address_t),             /* dhcp_header.hlen */
  0,                                        /* dhcp_header.hops */
  htonl(DHCP_XID),                          /* dhcp_header.xid */
  0,                                        /* dhcp_header.secs */
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
  DHCP_OPTION_PARAM_REQ, 5,
  DHCP_OPTION_BCAST_ADDR,
  DHCP_OPTION_SERVER_ID,
  DHCP_OPTION_TFTP_SERVER_NAME,
  DHCP_OPTION_TFTP_SERVER_ADDR,
  DHCP_OPTION_BOOTFILE,
  DHCP_OPTION_END
};

static const uint8_t dhcp_discover_options[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPDISCOVER
};

static const uint8_t dhcp_request_options_ipaddr[] = {
  DHCP_OPTION_MSG_TYPE, 1, DHCPREQUEST,
  DHCP_OPTION_REQ_IP_ADDR, 4      /* requested IP address follows */
};

static const uint8_t dhcp_request_options_server[] = {
  DHCP_OPTION_SERVER_ID, 4       /* server address follows */
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

/* Common beginning of all outgoing DHCP packets. */
static void
dhcp_add_header(void)
{
  udp_add(dhcp_sub_header);
  udp_add_w_len(ADDRESS_OF_ZEROS, DHCP_SIZEOF_HWADDR_PADDING
                                  + DHCP_SIZEOF_SNAME
                                  + DHCP_SIZEOF_FILE);

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
  print_at(0, 0, 20, 0, "SpeccyBoot " str(VERSION));
  print_at(23, 0, 22, 0, "DHCP");

  set_attrs(INK(WHITE) | PAPER(BLACK) | BRIGHT, 0, 0, 32);
  set_attrs(INK(WHITE) | PAPER(BLACK) | FLASH | BRIGHT, 23, 0, 4);

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

  uint8_t overload = 0;                          // option 52

  ipv4_address_t dhcp_server_addr
    = rx_frame.udp.app.dhcp.header.sub.siaddr;   // option 54

  uint8_t *tftp_server_name = NULL;              // option 66
  uint8_t tftp_server_name_length = 0;

  uint8_t *tftp_filename = NULL;                 // option 67
  uint8_t tftp_filename_length = 0;

  const ipv4_address_t *tftp_server_addr = NULL; // option 150

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
      case DHCP_OPTION_OVERLOAD:
        overload = *options;
        break;
      case DHCP_OPTION_SERVER_ID:
        dhcp_server_addr = *((const ipv4_address_t *) options);
        break;
      case DHCP_OPTION_TFTP_SERVER_NAME:
        tftp_server_name = (uint8_t *) options;
        tftp_server_name_length = option_length;
        break;
      case DHCP_OPTION_BOOTFILE:
        tftp_filename = (uint8_t *) options;
        tftp_filename_length = option_length;
        break;
      case DHCP_OPTION_TFTP_SERVER_ADDR:
        tftp_server_addr = options;
        break;
      case DHCP_OPTION_MSG_TYPE:
        msg_type = *options;
        break;
      }

      options += option_length;
    }

    // RFC 2132, section 9.3: if the SNAME field is not overloaded
    // to hold DHCP options, there seems to be a name there, and
    // no option 66 has been found, try to parse SNAME as a
    // server IP address.
    if (!(overload & 1)
        && tftp_server_name == NULL
        && rx_frame.udp.app.dhcp.header.sname[0])
    {
      tftp_server_name = rx_frame.udp.app.dhcp.header.sname;
    }
    if (!(overload & 2)
        && tftp_filename == NULL
        && rx_frame.udp.app.dhcp.header.file[0])
    {
      tftp_filename = rx_frame.udp.app.dhcp.header.sname;
    }
  }

  // parse server name after parsing all options, because then
  // a trailing NUL can be inserted (and simplify string parsing)
  if (tftp_server_addr == NULL && tftp_server_name != NULL) {
    tftp_server_name[tftp_server_name_length] = '\0';
    uint8_t *ip_addr = (uint8_t *) &ip_config.tftp_server_address;

    for (int i = 0; i < 4; i++) {
      uint8_t oct = (*tftp_server_name++) - '0';
      if (*tftp_server_name >= '0' && *tftp_server_name <= '9') {
        oct = (oct << 3) + (oct << 1) + (*tftp_server_name++) - '0';
        if (*tftp_server_name >= '0' && *tftp_server_name <= '9') {
          oct = (oct << 3) + (oct << 1) + (*tftp_server_name++) - '0';
        }
      }
      *ip_addr++ = oct;
      if (i < 3 && *tftp_server_name != '.') {
        fatal_error(FATAL_INVALID_BOOT_SERVER);
      }
      tftp_server_name++;
    }
  }

  if (tftp_filename != NULL) {
    tftp_filename[tftp_filename_length] = '\0';
  }

  switch (msg_type) {
  case DHCPACK:
    ip_config.host_address      = rx_frame.udp.app.dhcp.header.sub.yiaddr;
    ip_config.broadcast_address = broadcast_address;

    // The TFTP server address is chosen as follows:
    //
    // 1. Preferred case: TFTP server address (option 150)
    // 2. Next-best case: TFTP server name, with an IP address
    //    (e.g., 192.168.82.1 as a string).
    //    Nothing more needs to be done in this case: the IP address was set
    //    directly while parsing the server name (above).
    // 3. Fallback: try the same machine that responded to DHCP
    //
    // Don't use the broadcast address: some TFTP servers (e.g. dnsmasq)
    // won't respond to it.

    if (tftp_server_addr != NULL) {
      ip_config.tftp_server_address = *tftp_server_addr;
    } else if (tftp_server_name == NULL) {
      ip_config.tftp_server_address = rx_frame.udp.app.dhcp.header.sub.siaddr;
    }

    set_attrs(INK(WHITE) | PAPER(BLACK), 23, 0, 16);
    set_attrs(INK(WHITE) | PAPER(BLACK) | FLASH | BRIGHT, 23, 17, 15);

    print_at(23, 0, 22, 0, "Local:           TFTP:");

    print_ip_addr(&ip_config.host_address, (uint8_t *) (LOCAL_IP_POS));
    print_ip_addr(&ip_config.tftp_server_address, (uint8_t *) (SERVER_IP_POS));

    const char *f = SNAPSHOT_LIST_FILE;
    if (tftp_filename != NULL) {
      expect_snapshot();
      f = tftp_filename;
    }
    tftp_read_request(f);

    break;

  case DHCPOFFER:
    // The DHCP_REQUEST is broadcast, to ensure any other responding
    // DHCP server sees it too.
    udp_create_reply(SIZEOF_DHCP_REQUEST, true);
    dhcp_add_header();
    udp_add(dhcp_request_options_ipaddr);
    udp_add(rx_frame.udp.app.dhcp.header.sub.yiaddr);
    udp_add(dhcp_request_options_server);
    udp_add(dhcp_server_addr);
    dhcp_finalize_and_send();

    break;
  }
}
