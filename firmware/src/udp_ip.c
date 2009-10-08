/*
 * Module udp_ip:
 *
 * User Datagram Protocol (UDP, RFC 768)
 * Internet Protocol (IP, RFC 791)
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

#include "eth.h"
#include "udp_ip.h"
#include "context_switch.h"

#include "syslog.h"

#include "eth.h"

/* ------------------------------------------------------------------------- */

/*
 * Beginning of range of ephemeral port
 */
#define EPHEMERAL_PORT_START      ntohs(0xc000)

/*
 * Protocol indicating UDP payload in a IPv4 packet
 */
#define IP_PROTOCOL_UDP           (17)

/* ------------------------------------------------------------------------- */

uint16_t udp_port_tftp_client = EPHEMERAL_PORT_START;

/* ------------------------------------------------------------------------- */

struct ip_config_t ip_config = {
  IP_DEFAULT_HOST_ADDRESS,
  IP_DEFAULT_BCAST_ADDRESS
};

/* ------------------------------------------------------------------------- */

static uint16_t current_packet_length;

/* ------------------------------------------------------------------------- */

/*
 * IPv4 + UDP header prototype
 */
static PACKED_STRUCT() {
  struct ipv4_header_t ip;
  struct udp_header_t  udp;
} header_template = {
  {
    0x45,                     /* constant: version 4, 5 32-bit words in header */
    0x00,                     /* constant: type of service (normal) */
    0x0000,                   /* total length */
    0x0000,                   /* identification */
    0x0040,                   /* constant: fragmentation info */
    0x40,                     /* constant: time-to-live */
    IP_PROTOCOL_UDP,          /* constant: protocol */
    0x0000,                   /* checksum */
  },
  {
    0x0000,                   /* source port */
    0x0000,                   /* destination port */
    0x0000,                   /* UDP length */
    0x0000,                   /* constant: checksum (not used) */
  }
};

/* -------------------------------------------------------------------------
 * IP-style checksum computation
 * ------------------------------------------------------------------------- */

#define ip_checksum_add(_c, _data)  (ip_checksum_add_impl(&(_c),              \
                                     (&_data),                                \
                                     (sizeof(_data) >> 1)))

#define ip_checksum_value(_c)       (~(_c))

/*
 * Logical true if the computed IP-style checksum is OK.
 *
 * If the packet is intact, the computed sum is the sum of
 *   the actual packet contents, excluding checksum       (~_cs_rx)
 * + the checksum itself in one's complement              ( _cs_rx)
 *
 * Since X+~X == 0xffff for all X, the computed checksum must be 0xffff.
 */
#define ip_checksum_ok(_cs_comp)  ((_cs_comp) == 0xffffu)

/* -------------------------------------------------------------------------
 * Internal routine for ip_checksum_* macros above
 * ------------------------------------------------------------------------- */

static void
ip_checksum_add_impl(uint16_t *checksum,
                     const void *start_addr,
                     uint16_t nbr_words)
{
  /* -----------------------------------------------------------------------
   * Can't use the ENC28J60's checksum offload (errata, item #15)
   * ----------------------------------------------------------------------- */
  
  const uint16_t *p = (const uint16_t *) start_addr;

  while (nbr_words--) {
    uint32_t wide_sum = ((uint32_t) (*checksum)) + ((uint32_t) (*p++));
    *checksum = ((uint16_t) wide_sum) + (wide_sum >> 16);
  }
}

/* ------------------------------------------------------------------------- */

void
ip_frame_received(uint16_t eth_frame_length)
{
  /*
   * Read a minimal IPv4 header; if the actual header is longer (options), that
   * is handled below
   */
  uint16_t cs = eth_retrieve_payload(&rx_frame.ip,
                                     sizeof(struct ipv4_header_t),
                                     0);
  
  uint8_t  version_and_size  = rx_frame.ip.version_and_header_length;
  uint16_t fraginfo          = ntohs(rx_frame.ip.fragment_info & 0x1fffu);
  uint16_t ip_packet_length  = ntohs(rx_frame.ip.total_length);
  uint8_t  ip_header_size    = (version_and_size & 0x0f) << 2;
  uint16_t udp_packet_length = ip_packet_length - ip_header_size;
  
  /*
   * Once an IP address is set, multicasts/broadcasts are ignored
   */
  if (((ip_valid_address()) && (rx_frame.ip.dst_addr != ip_config.host_address))
      ||  (rx_frame.ip.prot != IP_PROTOCOL_UDP)
      ||  (version_and_size <  0x45)     /* IPv4 tag, minimal header length */
      ||  (version_and_size >  0x4f)     /* IPv4 tag, maximal header length */
      ||  (ip_packet_length >  eth_frame_length)
      ||         (fraginfo  != 0))
  {
    return;
  }

  /*
   * Discard options (if any) by reading them into UDP payload area
   * (unused so far), accumulating the IP checksum
   */
  if (ip_header_size > sizeof(struct ipv4_header_t)) {
    cs = eth_retrieve_payload(&rx_frame.udp.header,
                              ip_header_size - sizeof(struct ipv4_header_t),
                              cs);
  }
  
  if (! ip_checksum_ok(cs)) {
    syslog("bad IP checksum %, dropped", cs);
    return;
  }
  
  /*
   * Process UDP payload
   */
  cs = eth_retrieve_payload(&rx_frame.udp.header,
                            udp_packet_length,
                            htons(IP_PROTOCOL_UDP)); /* for pseudo IP header */
  
  if (   (udp_packet_length >= sizeof(struct udp_header_t))
      && (udp_packet_length >= ntohs(rx_frame.udp.header.length)))
  {
    if (rx_frame.udp.header.checksum != 0) {   /* valid UDP checksum */
      /*
       * Include IPv4 pseudo header in UDP checksum. The word for UDP protocol
       * was already included (given as initial value above), so we don't add
       * it here.
       */
      ip_checksum_add(cs, rx_frame.ip.src_addr);
      ip_checksum_add(cs, rx_frame.ip.dst_addr);
      ip_checksum_add(cs, rx_frame.udp.header.length);
      
      if (! ip_checksum_ok(cs)) {
        syslog("bad UDP checksum %, dropped", cs);
        return;
      }
    }
    
    if (rx_frame.udp.header.dst_port == udp_port_tftp_client) {
      tftp_packet_received(ntohs(rx_frame.udp.header.length)
                           - sizeof(struct udp_header_t));
    }
    else if (rx_frame.udp.header.dst_port == htons(UDP_PORT_DHCP_CLIENT)) {
      dhcp_packet_received();
    }
  }
  else {
    syslog("truncated packet (UDP says % bytes, IP says %)",
           ntohs(rx_frame.udp.header.length),
           udp_packet_length);
  }
}

/* ------------------------------------------------------------------------- */

void
udp_create_packet(const struct mac_address_t  *dst_hwaddr,
                  const ipv4_address_t        *dst_ipaddr,
                  uint16_t                     src_port_nw_endian,
                  uint16_t                     dst_port_nw_endian,
                  uint16_t                     udp_payload_length,
                  eth_frame_class_t            frame_class)
{
  uint16_t checksum = 0;

  uint16_t udp_length   = udp_payload_length + sizeof(struct udp_header_t);
  
  current_packet_length = udp_length + sizeof(struct ipv4_header_t);
  
  /*
   * Modify the header template with IP and UDP parameters, then write it to
   * the ENC28J60
   */
  header_template.ip.total_length   = htons(current_packet_length);
  header_template.ip.identification ++;
  header_template.ip.checksum       = 0;  /* temporary value for computation */
  header_template.ip.src_addr       = ip_config.host_address;
  header_template.ip.dst_addr       = *dst_ipaddr;
  
  ip_checksum_add(checksum, header_template.ip);
  header_template.ip.checksum       = ip_checksum_value(checksum);

  header_template.udp.src_port      = src_port_nw_endian;
  header_template.udp.dst_port      = dst_port_nw_endian;
  header_template.udp.length        = htons(udp_length);
  
  eth_create_frame(dst_hwaddr, ETHERTYPE_IP, frame_class);
  eth_add_payload_to_frame(&header_template, sizeof(header_template));
}

/* ------------------------------------------------------------------------- */

void
udp_create_reply(uint16_t udp_payload_length)
{
  udp_create_packet(&rx_eth_adm.eth_header.src_addr,
                    &rx_frame.ip.src_addr,
                    rx_frame.udp.header.dst_port,
                    rx_frame.udp.header.src_port,
                    udp_payload_length,
                    ETH_FRAME_PRIORITY);
}

/* ------------------------------------------------------------------------- */

void
udp_send_packet(void)
{
  eth_send_frame(current_packet_length);
}

