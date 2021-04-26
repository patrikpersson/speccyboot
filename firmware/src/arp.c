/*
 * Module arp:
 *
 * Address Resolution Protocol (ARP, RFC 826)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
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

#include "arp.h"

#include "eth.h"
#include "globals.h"

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

static bool
ip_valid_address()   // FIXME: remove this when rewriting this file
{
  uint8_t *p = (uint8_t *) &ip_config.host_address;
  return (*p != 0);
}

void
arp_receive(void)
{
  eth_retrieve_payload(&rx_frame.arp, sizeof(struct arp_ip_ethernet_t));

  if (    (rx_frame.arp.header.oper == htons(ARP_OPER_REQUEST))
      && (rx_frame.arp.header.ptype == htons(ETHERTYPE_IP))
      && (rx_frame.arp.header.htype == htons(ETH_HWTYPE))
      && (ip_valid_address())
      &&          (rx_frame.arp.tpa == ip_config.host_address))
  {
    eth_create(&rx_eth_adm.eth_header.src_addr,
	       htons(ETHERTYPE_ARP),
	       ETH_FRAME_OPTIONAL);

    eth_add(arp_ip_ethernet_reply_header);
    eth_add(eth_local_address);
    eth_add(ip_config.host_address);
    eth_add(rx_eth_adm.eth_header.src_addr);
    eth_add(rx_frame.arp.spa);

    eth_send(sizeof(struct arp_ip_ethernet_t));
  }
}
