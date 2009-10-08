/*
 * Module arp:
 *
 * Address Resolution Protocol (ARP, RFC 826)
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
#include "rxbuffer.h"

/* ========================================================================= */

/*
 * Opcodes for the ARP packets we support
 */

#define ARP_OPER_REQUEST   (1)
#define ARP_OPER_REPLY     (2)

/* -------------------------------------------------------------------------
 * Header data for IP-to-Ethernet ARP replies
 * ------------------------------------------------------------------------- */
static const struct arp_header_t arp_ip_ethernet_reply_header = {
  htons(ETH_HWTYPE),
  htons(ETHERTYPE_IP),
  sizeof(struct mac_address_t),
  sizeof(ipv4_address_t),
  htons(ARP_OPER_REPLY)
};

/* ========================================================================= */

void
arp_frame_received(uint16_t nbr_bytes_in_payload)
{
  if (nbr_bytes_in_payload >= sizeof(struct arp_ip_ethernet_t)) {
    (void) eth_retrieve_payload(&rx_frame.arp,
                                sizeof(struct arp_ip_ethernet_t),
                                0);
    
    /*
     * Check that the ARP packet concerns the right protocols
     * (IP-to-Ethernet mapping), that it is an ARP request, and that it is for
     * this host
     */
    if (    (rx_frame.arp.header.oper == htons(ARP_OPER_REQUEST))
        && (rx_frame.arp.header.ptype == htons(ETHERTYPE_IP))
        && (ip_valid_address())
        &&          (rx_frame.arp.tpa == ip_config.host_address)
        && (rx_frame.arp.header.htype == htons(ETH_HWTYPE))
        &&  (rx_frame.arp.header.hlen == sizeof(struct mac_address_t))
        &&  (rx_frame.arp.header.plen == sizeof(ipv4_address_t)))
    {
      eth_create_frame(&rx_eth_adm.eth_header.src_addr,
                       ETHERTYPE_ARP,
                       ETH_FRAME_OPTIONAL);
      
      eth_add_payload_to_frame(&arp_ip_ethernet_reply_header,
                               sizeof(struct arp_header_t));
      eth_add_payload_to_frame(eth_local_address.addr,
                               sizeof(struct mac_address_t));
      eth_add_payload_to_frame(&ip_config.host_address, sizeof(ipv4_address_t));
      eth_add_payload_to_frame(&rx_eth_adm.eth_header.src_addr,
                               sizeof(struct mac_address_t));
      eth_add_payload_to_frame(&rx_frame.arp.spa, sizeof(ipv4_address_t));
      
      eth_send_frame(sizeof(struct arp_ip_ethernet_t));
    }
  }
}
