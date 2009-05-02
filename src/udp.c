/*
 * Module udp:
 *
 * User Datagram Protocol (UDP, RFC 768)
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
#include "tftp.h"

#include "logging.h"

/* ------------------------------------------------------------------------- */

uint16_t tftp_port_nw_endian = 0;

/* ------------------------------------------------------------------------- */

void
udp_packet_received(const struct mac_address_t  *src_hwaddr,
                    const ipv4_address_t        *src,
                    const uint8_t               *payload,
                    uint16_t                     nbr_bytes_in_payload)
{
  const struct udp_header_t *header = (const struct udp_header_t *) payload;
  const uint8_t *udp_payload = payload + sizeof(struct udp_header_t);
  
  if (header->dst_port == tftp_port_nw_endian) {
    tftp_packet_received(src_hwaddr,
                         src,
                         header->src_port,
                         (const union tftp_packet_t *) udp_payload,
                         nbr_bytes_in_payload - sizeof(struct udp_header_t));
  }
  else if (header->dst_port == htons(UDP_PORT_DHCP_CLIENT)) {
    dhcp_packet_received(src, (const struct dhcp_header_t *) udp_payload);
  }
}

/* ------------------------------------------------------------------------- */

void
udp_create_packet(const struct mac_address_t  *dst_hwaddr,
                  const ipv4_address_t        *dst_ipaddr,
                  uint16_t                     src_port,
                  uint16_t                     dst_port,
                  uint16_t                     udp_payload_length)
{
  static const uint16_t zero = 0;
  uint16_t udp_length       = udp_payload_length + sizeof(struct udp_header_t);
  uint16_t nw_endian_length = htons(udp_length);
  
  ip_create_packet(dst_hwaddr,
                   dst_ipaddr,
                   udp_length,
                   IP_PROTOCOL_UDP,
                   ETH_FRAME_PRIORITY);
  
  /* UDP header */
  udp_add_payload_to_packet(src_port);
  udp_add_payload_to_packet(dst_port);
  udp_add_payload_to_packet(nw_endian_length);
  udp_add_payload_to_packet(zero);
}

/* ------------------------------------------------------------------------- */

void
udp_send_packet(uint16_t udp_payload_length)
{
  /*
   * Compute UDP checksum. Store the UDP length (as a component of the
   * IP pseudo-header) in the 'identification' field.
   */
  uint16_t udp_length       = udp_payload_length + sizeof(struct udp_header_t);
  uint16_t nw_endian_length = htons(udp_length);
  
  eth_rewrite_frame(offsetof(struct ipv4_header_t, identification),
                    &nw_endian_length,
                    sizeof(uint16_t),
                    ETH_FRAME_PRIORITY);
  
  eth_outgoing_ip_checksum(offsetof(struct ipv4_header_t, identification),
                           udp_length + (sizeof(struct ipv4_header_t)
                                         - offsetof(struct ipv4_header_t,
                                                    identification)),
                           sizeof(struct ipv4_header_t)
                           + offsetof(struct udp_header_t, checksum),
                           ETH_FRAME_PRIORITY);
  
  ip_send_packet(udp_length, ETH_FRAME_PRIORITY);
}
