/*
 * Module icmp:
 *
 * Internet Control Message Protocol (ICMP, RFC 792)
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

#include "icmp.h"
#include "ip.h"

#include "logging.h"

/* ------------------------------------------------------------------------- */

PACKED_STRUCT(icmp_header_t) {                /* ICMP header */
  uint8_t         type;
  uint8_t         code;
  uint16_t        checksum;
  uint16_t        id;
  uint16_t        seq;
};

/* ------------------------------------------------------------------------- */

/*
 * Supported ICMP operations
 */
#define ICMP_ECHO_REPLY           (0)
#define ICMP_ECHO_REQUEST         (8)

/* ------------------------------------------------------------------------- */

void
icmp_packet_received(const struct mac_address_t  *src_hwaddr,
                     const ipv4_address_t        *src,
                     const uint8_t               *payload,
                     uint16_t                     nbr_bytes_in_payload)
{
  struct icmp_header_t *header = (struct icmp_header_t *) payload;
  
  if (header->type == ICMP_ECHO_REQUEST) {
    /* Respond to ping by re-writing request into a reply */
    /*logging_add_entry("ICMP: got PING, 0x" HEX16_ARG " bytes",
                      (uint8_t *) &nbr_bytes_in_payload);*/
    header->type     = ICMP_ECHO_REPLY;
    header->checksum = 0;
      
    ip_create_packet(src_hwaddr,
                     src,
                     nbr_bytes_in_payload,
                     IP_PROTOCOL_ICMP,
                     ETH_FRAME_OPTIONAL);
    ip_add_payload_to_packet(payload, nbr_bytes_in_payload);

#if 0
    // FIXME
    /*
     * ICMP checksum
     */
    eth_outgoing_ip_checksum(sizeof(struct ipv4_header_t),
                             nbr_bytes_in_payload,
                             sizeof(struct ipv4_header_t)   // FIXME below
                               + 2/* offsetof(struct icmp_header_t, checksum)*/,
                             ETH_FRAME_OPTIONAL);
#endif
    
    ip_send_packet(nbr_bytes_in_payload, ETH_FRAME_OPTIONAL);
  }
  else {
    // logging_add_entry("ICMP: ignored operation 0x" HEX8_ARG, &header->type);
  }
}
