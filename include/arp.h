/*
 * Module arp:
 *
 * Address Resolution Protocol
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
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

#include "arp.h"
#include "ip.h"

/* ========================================================================= */

#define ETHERTYPE_ARP           (0x0806)

/* -------------------------------------------------------------------------
 * Called by eth.c when an Ethernet frame holding an ARP packet has been
 * received.
 *
 * If the frame informs us about the MAC address of the TFTP server, the
 * ip_server_address_resolved() function will be called.
 * ------------------------------------------------------------------------- */
void
arp_frame_received(const struct mac_address_t *src,
                   const uint8_t              *payload,
                   uint16_t                    nbr_bytes_in_payload);

#endif /* SPECCYBOOT_ARP_INCLUSION_GUARD */