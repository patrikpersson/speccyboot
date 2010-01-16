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

#include "udp_ip.h"

#include "eth.h"
#include "globals.h"
#include "syslog.h"

/* ------------------------------------------------------------------------- */

/* Global IP configuration */
struct ip_config_t ip_config = {
  IP_DEFAULT_HOST_ADDRESS,
  IP_DEFAULT_BCAST_ADDRESS
};

/* Length of currently constructed TX UDP packet */
static uint16_t current_packet_length;

/* Definitions for state exposed in header */
PACKED_STRUCT(header_template_t) header_template;
uint16_t tftp_client_port = htons(0xc000);

/* ------------------------------------------------------------------------- */

void
ip_receive(void)
{
  uint8_t ip_header_size;

  /* Read a minimal IPv4 header */
  ip_checksum_set(0);
  eth_retrieve_payload(&rx_frame.ip, sizeof(struct ipv4_header_t));
  
  ip_header_size = (rx_frame.ip.version_and_header_length & 0x0f) << 2;
  
  /* Once an IP address is set, multicasts/broadcasts are ignored */
  if (((ip_valid_address()) && (rx_frame.ip.dst_addr != ip_config.host_address))
      ||  (rx_frame.ip.prot != IP_PROTOCOL_UDP))
  {
    return;
  }

  /* Discard options (if any) */
  if (ip_header_size > sizeof(struct ipv4_header_t)) {
    eth_retrieve_payload(&rx_frame.udp.header,
			 ip_header_size - sizeof(struct ipv4_header_t));
  }
  
  if (! ip_checksum_ok()) {
    syslog("bad checksum");
    return;
  }
  
  /* Process UDP payload */
  ip_checksum_set(htons(IP_PROTOCOL_UDP)); /* for pseudo IP header */
  eth_retrieve_payload(&rx_frame.udp.header,
		       ntohs(rx_frame.ip.total_length) - ip_header_size);

  if (rx_frame.udp.header.checksum != 0) {   /* UDP checksum provided */
    /*
     * Include IPv4 pseudo header in UDP checksum. The word for UDP protocol
     * was already included (given as initial value above), so we don't add
     * it here.
     */
    ip_checksum_add_raw(rx_frame.ip.src_addr,
			2 * sizeof(ipv4_address_t));  /* src + dst */
    ip_checksum_add(rx_frame.udp.header.length);
      
    if (! ip_checksum_ok()) {
      syslog("bad checksum");
      return;
    }
  }
    
  if (ip_valid_address() && rx_frame.udp.header.dst_port == tftp_client_port) {
    tftp_receive();
  }
  else if (rx_frame.udp.header.dst_port == htons(UDP_PORT_DHCP_CLIENT)) {
    dhcp_receive();
  }
}

/* ------------------------------------------------------------------------- */

void
udp_create_impl(const struct mac_address_t  *dst_hwaddr,
		const ipv4_address_t        *dst_ipaddr,
		uint16_t                     udp_length,
		enc28j60_addr_t              frame_class)
{
  current_packet_length = udp_length + sizeof(struct ipv4_header_t);

  header_template.ip.version_and_header_length = 0x45;
  header_template.ip.type_of_service = 0;
  header_template.ip.total_length    = htons(current_packet_length);
  header_template.ip.id_and_fraginfo = 0x00400000; /* don't fragment */
  header_template.ip.time_to_live    = 0x40;
  header_template.ip.prot            = IP_PROTOCOL_UDP;
  header_template.ip.checksum        = 0;  /* temporary value for computation */
  header_template.ip.src_addr        = ip_config.host_address;
  header_template.ip.dst_addr        = *dst_ipaddr;
  
  ip_checksum_set(0);
  ip_checksum_add(header_template.ip);
  header_template.ip.checksum        = ip_checksum_value();

  header_template.udp.length         = htons(udp_length);
  header_template.udp.checksum       = 0;

  eth_create(dst_hwaddr, htons(ETHERTYPE_IP), frame_class);
  eth_add(header_template);
}

/* ------------------------------------------------------------------------- */

void
udp_create_reply(uint16_t udp_payload_length)
{
  udp_create(&rx_eth_adm.eth_header.src_addr,
	     &rx_frame.ip.src_addr,
	     rx_frame.udp.header.dst_port,
	     rx_frame.udp.header.src_port,
	     udp_payload_length,
	     ETH_FRAME_PRIORITY);
}

/* ------------------------------------------------------------------------- */

void
udp_send(void)
{
  eth_send(current_packet_length);
}

