/*
 * Module udp_ip:
 *
 * User Datagram Protocol (UDP, RFC 768)
 * Internet Protocol (IP, RFC 791)
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
#ifndef SPECCYBOOT_UDP_IP_INCLUSION_GUARD
#define SPECCYBOOT_UDP_IP_INCLUSION_GUARD

#include <stdbool.h>
#include <stdint.h>

#include "eth.h"
#include "util.h"

/* ========================================================================= */

/* Ethertype indicating IPv4 payload in an Ethernet frame */
#define ETHERTYPE_IP              (0x0800)

/* Protocol indicating UDP payload in a IPv4 packet */
#define IP_PROTOCOL_UDP           (17)

/* Static UDP ports */
#define UDP_PORT_BOOTP_SERVER     (67)
#define UDP_PORT_BOOTP_CLIENT     (68)
#define UDP_PORT_TFTP_SERVER      (69)
#define UDP_PORT_TFTP_CLIENT      (69)
#define UDP_PORT_SYSLOG           (514)

/* ========================================================================= */

/*
 * An IPv4 address: 32 bits, network order
 */
typedef uint32_t ipv4_address_t;

/* ------------------------------------------------------------------------- */

#define IPV4_HEADER_SIZE    (20)
#define IPV4_ADDRESS_SIZE   (4)

#define IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH        (0)
#define IPV4_HEADER_OFFSETOF_TOTAL_LENGTH              (2)
#define IPV4_HEADER_OFFSETOF_PROT                      (9)
#define IPV4_HEADER_OFFSETOF_CHECKSUM                 (10)
#define IPV4_HEADER_OFFSETOF_SRC_ADDR                 (12)
#define IPV4_HEADER_OFFSETOF_DST_ADDR                 (16)

/* ------------------------------------------------------------------------- */

#define UDP_HEADER_SIZE                      (8)

#define UDP_HEADER_OFFSETOF_SRC_PORT         (0)
#define UDP_HEADER_OFFSETOF_DST_PORT         (2)
#define UDP_HEADER_OFFSETOF_LENGTH           (4)
#define UDP_HEADER_OFFSETOF_CHECKSUM         (6)

/* ========================================================================= */

#define IP_CONFIG_HOST_ADDRESS_OFFSET   (0)
#define IP_CONFIG_TFTP_ADDRESS_OFFSET   (4)

/* ========================================================================= */

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
udp_create_impl(const struct mac_address_t  *dst_hwaddr,
		const ipv4_address_t        *dst_ipaddr,
		uint16_t                     udp_length);

#endif /* SPECCYBOOT_UDP_IP_INCLUSION_GUARD */
