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
#ifndef SPECCYBOOT_UDP_IP_INCLUSION_GUARD
#define SPECCYBOOT_UDP_IP_INCLUSION_GUARD

#include <stdint.h>

#include "eth.h"
#include "util.h"

/* ========================================================================= */

/* Ethertype indicating IPv4 payload in an Ethernet frame */
#define ETHERTYPE_IP              (0x0800)

/* Protocol indicating UDP payload in a IPv4 packet */
#define IP_PROTOCOL_UDP           (17)

/* Default (power-on) IP addresses */
#define IP_DEFAULT_HOST_ADDRESS   (0x00000000u)
#define IP_DEFAULT_BCAST_ADDRESS  (0xffffffffu)

/* Static UDP ports */
#define UDP_PORT_DHCP_SERVER      (67)
#define UDP_PORT_DHCP_CLIENT      (68)
#define UDP_PORT_TFTP_SERVER      (69)
#define UDP_PORT_TFTP_CLIENT      (69)
#define UDP_PORT_SYSLOG           (514)

/* ========================================================================= */

/*
 * An IPv4 address: 32 bits, network order
 */
typedef uint32_t ipv4_address_t;

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(udp_header_t) {                 /* UDP header */
  uint16_t        src_port;
  uint16_t        dst_port;
  uint16_t        length;
  uint16_t        checksum;
};

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(ipv4_header_t) {                /* IPv4 header (no options) */
  uint8_t         version_and_header_length;
  uint8_t         type_of_service;
  uint16_t        total_length;
  uint32_t        id_and_fraginfo;
  uint8_t         time_to_live;
  uint8_t         prot;
  uint16_t        checksum;
  ipv4_address_t  src_addr;
  ipv4_address_t  dst_addr;
};

/* ========================================================================= */

/* IP address configuration */
extern struct ip_config_t {
  ipv4_address_t host_address;
  ipv4_address_t broadcast_address;
} ip_config;

/* -------------------------------------------------------------------------
 * IP-style checksum computation
 * ------------------------------------------------------------------------- */

#define ip_checksum_add_raw(_data, _sz)                                       \
  enc28j60_add_checksum((&_data), ((_sz) >> 1))

#define ip_checksum_add(_data)                                                \
  ip_checksum_add_raw(_data, sizeof(_data)) 

#define ip_checksum_set           enc28j60_set_checksum

#define ip_checksum_value()       (~(enc28j60_get_checksum()))

/*
 * Logical true if the computed IP-style checksum is OK.
 *
 * If the packet is intact, the computed sum is the sum of
 *   the actual packet contents, excluding checksum       (~_cs_rx)
 * + the checksum itself in one's complement              ( _cs_rx)
 *
 * Since X+~X == 0xffff for all X, the computed checksum must be 0xffff.
 */
#define ip_checksum_ok()          (enc28j60_get_checksum() == 0xffffu)

/* -------------------------------------------------------------------------
 * Returns true if a valid IP address has been set
 * (assumes first byte is always non-zero when valid)
 * ------------------------------------------------------------------------- */
#define ip_valid_address()       (*((const uint8_t *) &ip_config.host_address))

/* -------------------------------------------------------------------------
 * Called by eth.c when an Ethernet frame holding an IP packet has been
 * received.
 * ------------------------------------------------------------------------- */
void
ip_receive(void);

/* -------------------------------------------------------------------------
 * Create UDP packet (IP + UDP headers).
 *
 * NOTE:
 *  - Source and destination ports are passed in network endian order.
 *  - The 'udp_length' argument must include sizeof(struct udp_header_t).
 * ------------------------------------------------------------------------- */
void
udp_create(const struct mac_address_t  *dst_hwaddr,
	   const ipv4_address_t        *dst_ipaddr,
	   uint16_t                     src_port_nw_endian,
	   uint16_t                     dst_port_nw_endian,
	   uint16_t                     udp_length,
	   eth_frame_class_t            frame_class);

/* -------------------------------------------------------------------------
 * Create UDP reply to the sender of the received packet currently processed.
 * Source/destination ports are swapped. Frame class is ETH_FRAME_PRIORITY.
 * ------------------------------------------------------------------------- */
void
udp_create_reply(uint16_t udp_payload_length);

/* -------------------------------------------------------------------------
 * Append payload to a UDP packet, previously created with udp_create() or
 * udp_create_reply()
 * ------------------------------------------------------------------------- */
#define udp_add(_data)  eth_add_w_len(&(_data), sizeof(_data))
#define udp_add_w_len   eth_add_w_len

/* -------------------------------------------------------------------------
 * Send a completed UDP packet
 * ------------------------------------------------------------------------- */
void
udp_send(void);

#endif /* SPECCYBOOT_UDP_IP_INCLUSION_GUARD */
