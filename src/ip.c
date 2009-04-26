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

#include <stddef.h>

#include "eth.h"
#include "ip.h"
#include "udp.h"
#include "icmp.h"

#include "spectrum.h"
#include "logging.h"

#include "eth.h"

#include "netboot.h"

/* ------------------------------------------------------------------------- */

struct ip_config_t ip_config = {
  IP_DEFAULT_HOST_ADDRESS,
  IP_DEFAULT_BCAST_ADDRESS
};

/* ------------------------------------------------------------------------- */

static void
ip_create_packet(const struct mac_address_t  *dst_hwaddr,
                 const ipv4_address_t        *dst,
                 uint16_t                     total_length,
                 uint8_t                      protocol,
                 enum eth_frame_class_t       frame_class)
{
  /*
   * IPv4 header prototype for transmitted packets. Most of these fields are
   * set to zero, to allow for UDP checksum calculations. The remaining ones
   * are set in ip_send_packet() below.
   */
  static struct ipv4_header_t header_template = {
    0x45,                     /* version 4, 5 32-bit words in header */
    0x00,                     /* type of service (normal) */
    0x0000,                   /* total length */
    0x0000,                   /* identification */
    0x0000,                   /* fragmentation info */
    0x00,                     /* time-to-live */
    0x0000,                   /* protocol */
    0x0000,                   /* checksum */
  };
  
  header_template.total_length = htons(total_length
                                 + sizeof(struct ipv4_header_t));
  header_template.protocol     = protocol;
  header_template.src_addr     = ip_config.host_address;
  header_template.dst_addr     = *dst;
  
  eth_create_frame(dst_hwaddr, ETHERTYPE_IP, frame_class);
  eth_add_payload_to_frame(&header_template, sizeof(struct ipv4_header_t));
}

/* ------------------------------------------------------------------------- */

static void
ip_send_packet(uint16_t                total_nbr_of_bytes_in_payload,
               enum eth_frame_class_t  frame_class)
{
  /*
   * Constant values for IPv4 header elements. Not written in
   * ip_create_packet() to allow for UDP checksum calculations.
   */
  static const PACKED_STRUCT(constant_header_data_t) {
    uint16_t  fragment_info;
    uint8_t   time_to_live;
  } constant_header_data = { 0,0xff /*htons(0x4000), 0x40*/ };    // FIXME
  
  eth_rewrite_frame(offsetof(struct ipv4_header_t, fragment_info),
                    &constant_header_data,
                    sizeof(constant_header_data),
                    frame_class);
  
  /* IPv4 checksum (for header) */
  eth_outgoing_ip_checksum(0,
                           sizeof(struct ipv4_header_t),
                           offsetof(struct ipv4_header_t, checksum),
                           frame_class);
  eth_send_frame(total_nbr_of_bytes_in_payload + sizeof(struct ipv4_header_t),
                 frame_class);
}

/* ------------------------------------------------------------------------- */

void
ip_frame_received(const struct mac_address_t *src_hwaddr,
                  const uint8_t              *payload,
                  uint16_t                    nbr_bytes_in_payload)
{
  const struct ipv4_header_t *header = (const struct ipv4_header_t *) payload;
  
  uint8_t  version_and_size = header->version_and_header_length;
  uint16_t fraginfo         = ntohs(header->fragment_info & 0x1fffu);
  uint16_t ip_total_len     = ntohs(header->total_length);
  uint8_t  header_size      = (version_and_size & 0x0f) << 2;
  
  /*
   * Once an IP address is set, multicasts/broadcasts are ignored
   */
  if (((ip_valid_address()) && (header->dst_addr != ip_config.host_address))
      || (version_and_size <  0x45)     /* IPv4 tag, minimal header length */
      || (version_and_size >  0x4f)     /* IPv4 tag, maximal header length */
      ||     (ip_total_len >  nbr_bytes_in_payload)
      ||        (fraginfo  != 0))
  {
    return;
  }

  switch (header->protocol) {
    case IP_PROTOCOL_UDP:
      udp_packet_received(src_hwaddr,
                          &(header->src_addr),
                          payload + header_size,
                          ip_total_len - header_size);
      break;
    case IP_PROTOCOL_ICMP:
      icmp_packet_received(src_hwaddr,
                           &(header->src_addr),
                           payload + header_size,
                           ip_total_len - header_size);
      break;
    default:
      logging_add_entry("IP: unknown protocol " DEC8_ARG, &header->protocol);
      break;
  }
}
