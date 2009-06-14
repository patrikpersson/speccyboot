/*
 * Module arp:
 *
 * Address Resolution Protocol
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

#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "logging.h"

/* ========================================================================= */

/*
 * Opcodes for the ARP packets we support
 */

#define ARP_OPER_REQUEST   (1)
#define ARP_OPER_REPLY     (2)

/* ------------------------------------------------------------------------- */

/*
 * ARP header (general)
 */
PACKED_STRUCT(arp_header_t) {
  uint16_t htype;
  uint16_t ptype;
  uint8_t  hlen;
  uint8_t  plen;
  uint16_t oper;
};

/*
 * ARP packet for IP-to-Ethernet mapping
 */
PACKED_STRUCT(arp_ip_ethernet_t) {
  struct arp_header_t  header;
  
  struct mac_address_t sha;
  ipv4_address_t       spa;
  struct mac_address_t tha;
  ipv4_address_t       tpa;
};

/* ========================================================================= */

void
arp_announce(void)
{
  static const struct arp_header_t arp_ip_ethernet_announce_header = {
    /* Header data for IP-to-Ethernet ARP announcements (using ARP REQUEST) */
    htons(ETH_HWTYPE),
    htons(ETHERTYPE_IP),
    sizeof(struct mac_address_t),
    sizeof(ipv4_address_t),
    htons(ARP_OPER_REQUEST)
  };

  eth_create_frame(&eth_broadcast_address, ETHERTYPE_ARP, ETH_FRAME_OPTIONAL);
  
  eth_add_payload_to_frame(&arp_ip_ethernet_announce_header,
                           sizeof(struct arp_header_t));
  eth_add_payload_to_frame(eth_local_address.addr,
                           sizeof(struct mac_address_t));
  eth_add_payload_to_frame(&ip_config.host_address,
                           sizeof(ipv4_address_t));
  eth_add_payload_to_frame(&eth_broadcast_address,
                           sizeof(struct mac_address_t));
  eth_add_payload_to_frame(&ip_config.host_address,
                           sizeof(ipv4_address_t));
  
  eth_send_frame(sizeof(struct arp_ip_ethernet_t), ETH_FRAME_OPTIONAL);
}

/* ------------------------------------------------------------------------- */

void
arp_frame_received(const struct mac_address_t *src,
                   const uint8_t              *payload,
                   uint16_t                    nbr_bytes_in_payload)
{
  const struct arp_ip_ethernet_t *arp_packet
    = (const struct arp_ip_ethernet_t *) payload;
  
  /*
   * Check that the ARP packet concerns the right protocols
   * (IP-to-Ethernet mapping), that it is an ARP request, and that it is for
   * this host
   */
  if (       (nbr_bytes_in_payload >=  sizeof(struct arp_ip_ethernet_t))
      &&  (arp_packet->header.oper == htons(ARP_OPER_REQUEST))
      && (arp_packet->header.ptype == htons(ETHERTYPE_IP))
      && (ip_valid_address())
      &&          (arp_packet->tpa == ip_config.host_address)
      && (arp_packet->header.htype == htons(ETH_HWTYPE))
      &&  (arp_packet->header.hlen == sizeof(struct mac_address_t))
      &&  (arp_packet->header.plen == sizeof(ipv4_address_t)))
  {
    static const struct arp_header_t arp_ip_ethernet_reply_header = {
      /* Header data for IP-to-Ethernet ARP replies */
      htons(ETH_HWTYPE),
      htons(ETHERTYPE_IP),
      sizeof(struct mac_address_t),
      sizeof(ipv4_address_t),
      htons(ARP_OPER_REPLY)
    };

    log_info("ARP", "REQUEST for %a from %a",
             &arp_packet->tpa,
             &arp_packet->spa);

    eth_create_frame(src, ETHERTYPE_ARP, ETH_FRAME_OPTIONAL);
    
    eth_add_payload_to_frame(&arp_ip_ethernet_reply_header,
                             sizeof(struct arp_header_t));
    eth_add_payload_to_frame(eth_local_address.addr,
                             sizeof(struct mac_address_t));
    eth_add_payload_to_frame(&ip_config.host_address, sizeof(ipv4_address_t));
    eth_add_payload_to_frame(src, sizeof(struct mac_address_t));
    eth_add_payload_to_frame(&arp_packet->spa, sizeof(ipv4_address_t));
    
    eth_send_frame(sizeof(struct arp_ip_ethernet_t), ETH_FRAME_OPTIONAL);
  }
}
