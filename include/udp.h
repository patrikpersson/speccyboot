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
#ifndef SPECCYBOOT_UDP_INCLUSION_GUARD
#define SPECCYBOOT_UDP_INCLUSION_GUARD

#include <stdint.h>

#include "eth.h"
#include "ip.h"

/*
 * Static UDP ports
 */
#define UDP_PORT_DHCP_SERVER      (67)
#define UDP_PORT_DHCP_CLIENT      (68)
#define UDP_PORT_TFTP_SERVER      (69)

/* -------------------------------------------------------------------------
 * TFTP (ephemeral) port management
 *
 * NOTE: this value is always used in network byte order (i.e., it is not
 *       necessary to use htons() or ntohs() on it)
 * ------------------------------------------------------------------------- */

extern uint16_t tftp_port_nw_endian;    /* Access using macros below */

#define udp_set_tftp_port(p)      tftp_port_nw_endian = (p)
#define udp_get_tftp_port()       (tftp_port_nw_endian)

/* -------------------------------------------------------------------------
 * UDP header
 * ------------------------------------------------------------------------- */

PACKED_STRUCT(udp_header_t) {
  uint16_t        src_port;
  uint16_t        dst_port;
  uint16_t        length;
  uint16_t        checksum;
};

/* -------------------------------------------------------------------------
 * IP pseudo-header for UDP checksum computation
 * ------------------------------------------------------------------------- */

PACKED_STRUCT(udp_ip_pseudo_header_t) {
  ipv4_address_t  src_addr;
  ipv4_address_t  dst_addr;
  uint8_t         zero;         /* always zero */
  uint8_t         protocol;     /* always for UDP */
  uint16_t        udp_length;
};

/* -------------------------------------------------------------------------
 * Called by IP when a UDP packet has been identified
 * ------------------------------------------------------------------------- */
void
udp_packet_received(const struct mac_address_t  *src_hwaddr,
                    const ipv4_address_t        *src,
                    const ipv4_address_t        *dst,
                    const uint8_t               *payload);

/* -------------------------------------------------------------------------
 * Create UDP packet
 *
 * NOTE: source and destination ports are passed in network endian order.
 * ------------------------------------------------------------------------- */
void
udp_create_packet(const struct mac_address_t  *dst_hwaddr,
                  const ipv4_address_t        *dst_ipaddr,
                  uint16_t                     src_port_nw_endian,
                  uint16_t                     dst_port_nw_endian,
                  uint16_t                     udp_length,
                  enum eth_frame_class_t       frame_class);

/* -------------------------------------------------------------------------
 * Append payload to a UDP packet, previously created with udp_create_packet()
 * ------------------------------------------------------------------------- */
#define  udp_add_payload_to_packet(_data)                                     \
  ip_add_payload_to_packet(&(_data), sizeof(_data))

#define  udp_add_variable_payload_to_packet(_data, _len)                      \
  ip_add_payload_to_packet((_data), (_len))

/* -------------------------------------------------------------------------
 * Send a completed UDP packet.
 *
 * The 'udp_length' field MUST match the one passed to udp_create_packet().
 * ------------------------------------------------------------------------- */
#define udp_send_packet(_udp_payload_length, _udp_frame_class)                \
  ip_send_packet((_udp_payload_length) + sizeof(struct udp_header_t),         \
                 (_udp_frame_class))

#endif /* SPECCYBOOT_UDP_INCLUSION_GUARD */
