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

#include "rxbuffer.h"

#include "udp.h"

#include "dhcp.h"
#include "tftp.h"

#include "syslog.h"

/* ------------------------------------------------------------------------- */

/*
 * Beginning of range of ephemeral port
 */
#define EPHEMERAL_PORT_START      ntohs(0xc000)

/*
 * Constant data used for IPv4 pseudo header in udp_packet_received()
 */
static const uint16_t pseudo_header_prot = htons(IP_PROTOCOL_UDP);

/* ------------------------------------------------------------------------- */

uint16_t udp_port_tftp_client = EPHEMERAL_PORT_START;

/* ------------------------------------------------------------------------- */

void
udp_packet_received(uint16_t udp_packet_length)
{
  uint16_t cs = ip_retrieve_payload(&rx_frame.udp.header,
                                    udp_packet_length,
                                    0);
  
  if (   (udp_packet_length >= sizeof(struct udp_header_t))
      && (udp_packet_length >= ntohs(rx_frame.udp.header.length)))
  {
    if (rx_frame.udp.header.checksum != 0) {   /* valid UDP checksum */
      /*
       * Include IPv4 pseudo header in UDP checksum
       */
      ip_checksum_add(cs, &pseudo_header_prot,         sizeof(uint16_t));
      ip_checksum_add(cs, &rx_frame.ip.src_addr,       sizeof(ipv4_address_t));
      ip_checksum_add(cs, &rx_frame.ip.dst_addr,       sizeof(ipv4_address_t));
      ip_checksum_add(cs, &rx_frame.udp.header.length, sizeof(uint16_t));
      
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
                  enum eth_frame_class_t       frame_class)
{
  uint16_t udp_length = udp_payload_length + sizeof(struct udp_header_t);
  
  ip_create_packet(dst_hwaddr,
                   dst_ipaddr,
                   udp_length,
                   IP_PROTOCOL_UDP,
                   frame_class);
  
  /* UDP header */
  udp_add_payload_to_packet(src_port_nw_endian);
  udp_add_payload_to_packet(dst_port_nw_endian);
  udp_add_payload_nwu16_to_frame(udp_length);
  udp_add_payload_byte_to_packet(0);
  udp_add_payload_byte_to_packet(0);
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