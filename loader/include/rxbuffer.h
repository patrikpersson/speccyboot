/*
 * Module rxbuffer:
 *
 * Buffer for received frame (global)
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
#include "udp.h"
#include "arp.h"
#include "dhcp.h"
#include "tftp.h"

/* ------------------------------------------------------------------------- */

/*
 * Administrative Ethernet information, including Ethernet header
 */
extern struct eth_adm_t           rx_eth_adm;

/* ------------------------------------------------------------------------- */

/*
 * This union is NOT designed for reading an entire Ethernet frame in one go:
 * this is not practical since, for example, the IP header has variable size.
 *
 * Instead, the purpose of this union is to preserve static memory by allowing
 * buffers to overlap whenever possible.
 */
extern union rx_frame_t {
  /* --------------------------------------------------------- Raw IP header */
  struct ipv4_header_t            ip;
  
  /* ------------------------------------------------------------------- UDP */
  PACKED_STRUCT() {
    struct ipv4_header_t          ip_header;
    struct udp_header_t           header;
    
    union {
      struct dhcp_packet_t        dhcp;
      struct tftp_data_packet_t   tftp;
    } app;
  } udp;
  
  /* ------------------------------------------------------------------- ARP */
  struct arp_ip_ethernet_t        arp;
} rx_frame;
