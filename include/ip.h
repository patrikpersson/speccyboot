/*
 * Module ip:
 *
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
#ifndef SPECCYBOOT_IP_INCLUSION_GUARD
#define SPECCYBOOT_IP_INCLUSION_GUARD

#include <stdint.h>

#include "util.h"
#include "platform.h"

/* ========================================================================= */

#define ETHERTYPE_IP            (0x0800)

/*
 * Supported IP protocols
 */
#define IP_PROTOCOL_ICMP          (1)
#define IP_PROTOCOL_UDP           (17)

/*
 * Default (power-on) IP addresses
 */
#define IP_DEFAULT_HOST_ADDRESS   (0x00000000u)
#define IP_DEFAULT_BCAST_ADDRESS  (0xffffffffu)

/*
 * Maximal payload handled:
 *  60   (max IP header size)
 *   8   (UDP header)
 * 576   (maximal accepted DHCP packet size; minimal value allowed by RFC)
 * ---
 * 644 bytes
 */
#define IP_MAX_PAYLOAD          (644)

/*
 * An IPv4 address: 32 bits, network order
 */
typedef uint32_t ipv4_address_t;

/*
 * Defined in eth.h
 */
struct mac_address_t;

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(ipv4_header_t) {                /* IPv4 header (no options) */
  uint8_t         version_and_header_length;
  uint8_t         type_of_service;
  uint16_t        total_length;
  uint16_t        identification;
  uint16_t        fragment_info;
  uint8_t         time_to_live;
  uint8_t         protocol;
  uint16_t        checksum;
  ipv4_address_t  src_addr;
  ipv4_address_t  dst_addr;
};

/* ------------------------------------------------------------------------- */

/*
 * IP address configuration
 */
extern struct ip_config_t {
  ipv4_address_t host_address;
  ipv4_address_t broadcast_address;
} ip_config;

/* ------------------------------------------------------------------------- */

/*
 * Maximal (worst-case) IP header size
 */
#define IP_MAX_HEADER_SIZE        (60)

/* -------------------------------------------------------------------------
 * Returns true if a valid IP address has been set
 * ------------------------------------------------------------------------- */

#define ip_valid_address()        (ip_config.host_address != 0x00000000)

/* -------------------------------------------------------------------------
 * IP-style checksum computation
 * ------------------------------------------------------------------------- */

typedef uint16_t ip_checksum_state_t;

#define ip_checksum_add(_c, _addr, _n)  (ip_checksum_add_impl(&(_c), (_addr), (_n)))
#define ip_checksum_value(_c)           (~(_c))

/* -------------------------------------------------------------------------
 * Called by eth.c when an Ethernet frame holding an IP packet has been
 * received.
 *
 * If the frame is a TFTP data packet, the function netboot_receive_data()
 * will be called.
 *
 * The 'address_in_eth' argument points to the address of the received IP
 * payload in ENC28J60 memory (for checksum verification).
 * ------------------------------------------------------------------------- */
void
ip_frame_received(const struct mac_address_t *src,
                  const uint8_t              *payload,
                  uint16_t                    nbr_bytes_in_payload);

/* -------------------------------------------------------------------------
 * Create an IP packet with header
 * ------------------------------------------------------------------------- */
void
ip_create_packet(const struct mac_address_t  *dst_hwaddr,
                 const ipv4_address_t        *dst,
                 uint16_t                     total_length,
                 uint8_t                      protocol,
                 enum eth_frame_class_t       frame_class);

/* -------------------------------------------------------------------------
 * Append payload to a packet previously created with ip_create_packet()
 * ------------------------------------------------------------------------- */
#define ip_add_payload_to_packet      eth_add_payload_to_frame

/* -------------------------------------------------------------------------
 * Send an IP packet, previously created with ip_create_packet()
 *
 * total_nbr_of_bytes_in_payload:   number of bytes in payload, excluding
 *                                  header.
 * ------------------------------------------------------------------------- */
#define ip_send_packet(_total_nbr_of_bytes_in_ip_payload, _ip_frame_class)    \
  eth_send_frame((_total_nbr_of_bytes_in_ip_payload)                          \
                   + sizeof(struct ipv4_header_t),                            \
                 (_ip_frame_class))

/* -------------------------------------------------------------------------
 * Internal routine for ip_checksum_* macros above
 * ------------------------------------------------------------------------- */
void
ip_checksum_add_impl(uint16_t *checksum,
                     const void *start_addr,
                     uint16_t nbr_bytes);

#endif /* SPECCYBOOT_ETH_IP_INCLUSION_GUARD */
