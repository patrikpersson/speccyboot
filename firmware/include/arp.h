/*
 * Module arp:
 *
 * Address Resolution Protocol
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
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
#ifndef SPECCYBOOT_ARP_INCLUSION_GUARD
#define SPECCYBOOT_ARP_INCLUSION_GUARD

#include "udp_ip.h"

/* ========================================================================= */

#define ETHERTYPE_ARP           (0x0806)

/* ARP header */
PACKED_STRUCT(arp_header_t) {
  uint16_t htype;
  uint16_t ptype;
  uint8_t  hlen;
  uint8_t  plen;
  uint16_t oper;
};

/* ARP packet for IP-to-Ethernet mapping */
PACKED_STRUCT(arp_ip_ethernet_t) {
  struct arp_header_t  header;
  
  struct mac_address_t sha;
  ipv4_address_t       spa;
  struct mac_address_t tha;
  ipv4_address_t       tpa;
};

/* -------------------------------------------------------------------------
 * Called by eth.c when an Ethernet frame holding an ARP packet has been
 * received.
 * ------------------------------------------------------------------------- */
void
arp_receive(void);

#endif /* SPECCYBOOT_ARP_INCLUSION_GUARD */
