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

#include "logging.h"

#include "eth.h"

/* ------------------------------------------------------------------------- */

struct ip_config_t ip_config = {
  IP_DEFAULT_HOST_ADDRESS,
  IP_DEFAULT_BCAST_ADDRESS
};

/* ------------------------------------------------------------------------- */

/*
 * IPv4 header prototype
 */
static struct ipv4_header_t header_template = {
  0x45,                     /* version 4, 5 32-bit words in header */
  0x00,                     /* type of service (normal) */
  0x0000,                   /* total length */
  0x0000,                   /* identification */
  0x0040,                   /* fragmentation info */
  0x40,                     /* time-to-live */
  0x0000,                   /* protocol */
  0x0000,                   /* checksum */
};

/* ------------------------------------------------------------------------- */

static bool
ip_header_checksum(const struct ipv4_header_t *header,
                   uint16_t                    header_size)
{
  uint16_t option_size = header_size - sizeof(struct ipv4_header_t);
  ip_checksum_state_t checksum = 0;

  ip_checksum_add(checksum, header, offsetof(struct ipv4_header_t, checksum));
  ip_checksum_add(checksum,
                  &(header->src_addr),
                  2 * sizeof(ipv4_address_t));  /* src + dst */
  if (option_size != 0) {
    ip_checksum_add(checksum,
                    ((const uint8_t *) header) + sizeof(struct ipv4_header_t),
                    option_size);
  }
  
  if (ip_checksum_value(checksum) != header->checksum) {
    log_warning("IP",
                "checksum mismatch in frame 0x%x "
                "(received 0x%x, computed 0x%x), packet dropped",
                header->identification,
                header->checksum,
                ip_checksum_value(checksum));
    
    return false;
  }
  
  return true;
}

/* ------------------------------------------------------------------------- */

void
ip_create_packet(const struct mac_address_t  *dst_hwaddr,
                 const ipv4_address_t        *dst,
                 uint16_t                     total_length,
                 uint8_t                      protocol,
                 enum eth_frame_class_t       frame_class)
{
  ip_checksum_state_t checksum = 0;

  /*
   * Modify the header template, and write it to the ENC28J60
   */
  header_template.total_length   = htons(total_length
                                   + sizeof(struct ipv4_header_t));
  header_template.identification ++;
  header_template.protocol       = protocol;
  header_template.checksum       = 0;      /* temporary value for computation */
  header_template.src_addr       = ip_config.host_address;
  header_template.dst_addr       = *dst;

  ip_checksum_add(checksum, &header_template, sizeof(struct ipv4_header_t));
  header_template.checksum       = ip_checksum_value(checksum);
  
  eth_create_frame(dst_hwaddr, ETHERTYPE_IP, frame_class);
  eth_add_payload_to_frame(&header_template, sizeof(struct ipv4_header_t));
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
      ||  (version_and_size <  0x45)     /* IPv4 tag, minimal header length */
      ||  (version_and_size >  0x4f)     /* IPv4 tag, maximal header length */
      ||      (ip_total_len >  nbr_bytes_in_payload)
      ||         (fraginfo  != 0)
      || !ip_header_checksum(header, header_size))
  {
    return;
  }
  
  switch (header->protocol) {
    case IP_PROTOCOL_UDP:
        udp_packet_received(src_hwaddr,
                            &(header->src_addr),
                            &(header->dst_addr),
                            payload + header_size);
      break;
    case IP_PROTOCOL_ICMP:
      icmp_packet_received(src_hwaddr,
                           &(header->src_addr),
                           payload + header_size,
                           ip_total_len - header_size);
      break;
    default:
      break;
  }
}

/* -------------------------------------------------------------------------
 * Can't use the ENC28J60's checksum offload (errata, item #15)
 * ------------------------------------------------------------------------- */
void
ip_checksum_add_impl(uint16_t *checksum,
                     const void *start_addr,
                     uint16_t nbr_bytes)
{
  uint16_t nbr_words = nbr_bytes >> 1;
  const uint16_t *p = (const uint16_t *) start_addr;
  /* FIXME: odd-sized payloads */
  while (nbr_words--) {
    uint32_t wide_sum = ((uint32_t) (*checksum)) + ((uint32_t) (*p++));
    *checksum = ((uint16_t) wide_sum) + (wide_sum >> 16);
  }
}