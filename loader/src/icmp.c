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
#include "rxbuffer.h"

#include "syslog.h"

/* ------------------------------------------------------------------------- */

/*
 * Supported ICMP operations
 */
#define ICMP_ECHO_REPLY           (0)
#define ICMP_ECHO_REQUEST         (8)

/* ------------------------------------------------------------------------- */

void
icmp_packet_received(uint16_t nbr_bytes_in_payload)
{
  if (  (rx_frame.icmp.header.type == ICMP_ECHO_REQUEST)
      &&     (nbr_bytes_in_payload >= sizeof(rx_frame.icmp)))
  {
    uint16_t cs = ip_retrieve_payload(&rx_frame.icmp.header,
                                       nbr_bytes_in_payload,
                                       0);
    
    /*
     * Respond to ping by re-writing request into a reply, with updated
     * checksum
     *
     * TODO: check checksum
     */
    
    (void) cs;
    
    rx_frame.icmp.header.type     = ICMP_ECHO_REPLY;
    rx_frame.icmp.header.checksum = 0;
    ip_checksum_add(rx_frame.icmp.header.checksum,
                    &rx_frame.icmp,
                    nbr_bytes_in_payload);
    rx_frame.icmp.header.checksum
      = ip_checksum_value(rx_frame.icmp.header.checksum);
    
    ip_create_packet(&rx_eth_adm.eth_header.src_addr,
                     rx_frame.ip.src_addr,
                     nbr_bytes_in_payload,
                     IP_PROTOCOL_ICMP,
                     ETH_FRAME_OPTIONAL);
    ip_add_payload_to_packet(&rx_frame.icmp, nbr_bytes_in_payload);
    
    ip_send_packet();
  }
}
