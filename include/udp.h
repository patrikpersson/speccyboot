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

/*
 * Ephemeral port for TFTP client, stored in network order
 */
extern uint16_t udp_port_tftp_client;

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
 * Called by IP when a UDP packet has been identified
 * ------------------------------------------------------------------------- */
void
udp_packet_received(uint16_t udp_packet_length);

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
                  uint16_t                     udp_payload_length,
                  enum eth_frame_class_t       frame_class);

/* -------------------------------------------------------------------------
 * Create UDP reply to the sender of the most recently received packet.
 * Source/destination ports are swapped. Frame class is ETH_FRAME_PRIORITY.
 * ------------------------------------------------------------------------- */
void
udp_create_reply(uint16_t udp_payload_length);

/* -------------------------------------------------------------------------
 * Append payload to a UDP packet, previously created with udp_create_packet()
 * ------------------------------------------------------------------------- */
#define  udp_add_payload_to_packet(_data)                                     \
  ip_add_payload_to_packet(&(_data), sizeof(_data))

#define  udp_add_variable_payload_to_packet(_data, _len)                      \
  ip_add_payload_to_packet((_data), (_len))

#define udp_add_payload_byte_to_packet      ip_add_payload_byte_to_packet
#define udp_add_payload_nwu16_to_frame      ip_add_payload_nwu16_to_frame

/* -------------------------------------------------------------------------
 * Send a completed UDP packet.
 * ------------------------------------------------------------------------- */
#define udp_send_packet   ip_send_packet

/* -------------------------------------------------------------------------
 * Allocate a new ephemeral port for TFTP client
 * (network order)
 * ------------------------------------------------------------------------- */
#define udp_new_tftp_port()                                                   \
  udp_port_tftp_client += 0x0100

#endif /* SPECCYBOOT_UDP_INCLUSION_GUARD */
