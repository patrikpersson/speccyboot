/*
 * Module arp:
 *
 * Address Resolution Protocol (ARP, RFC 826)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
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

#include "udp_ip.h"

/* ========================================================================= */

#define ARP_HEADER_SIZE         (8)
#define ARP_OFFSET_SPA          (14)
#define ARP_OFFSET_TPA          (24)

/* Size of an ARP packet for an IP-Ethernet mapping */
#define ARP_IP_ETH_PACKET_SIZE  (ARP_HEADER_SIZE + 20)

/* ========================================================================= */

void
arp_receive(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; retrieve ARP payload
    ;; ------------------------------------------------------------------------

    ld   bc, #ARP_IP_ETH_PACKET_SIZE
    push bc
    ld   hl, #_rx_frame
    push hl
    call _enc28j60_read_memory_cont
    pop  hl
    pop  bc

    ;; ------------------------------------------------------------------------
    ;; check header against template
    ;; (ARP_OPER_REQUEST, ETHERTYPE_IP, ETH_HWTYPE)
    ;; ------------------------------------------------------------------------

    ;; first check everything except OPER

    ld   de, #arp_receive_reply_template
    ld   b, #(ARP_HEADER_SIZE - 1)
    call _memory_compare
    ret  nz   ;; if the receive packet does not match the expected header, return

    ;; HL now points to the low-order OPER byte, expected to be 1 (REQUEST)
    ld   a, (hl)
    dec  a
    ret  nz

    ;; ------------------------------------------------------------------------
    ;; check that a local IP address has been set,
    ;; and that the packet was sent to this address
    ;; ------------------------------------------------------------------------

    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld   a, (hl)
    or   a, a
    ret  z

    ld   de , #_rx_frame + ARP_OFFSET_TPA
    ld   b, #IPV4_ADDRESS_SIZE
    call _memory_compare
    ret  nz   ;; if the packet is not for the local IP address, return

    ;; ========================================================================
    ;; create ARP response
    ;; ========================================================================

    ld   hl, #ETH_FRAME_OPTIONAL
    push hl
    ld   hl, #0x0608           ;; ETHERTYPE_ARP, network order
    push hl
    ld   hl, #_rx_eth_adm + ETH_ADM_OFFSETOF_SRC_ADDR
    push hl
    call _eth_create
    pop  hl
    pop  hl
    pop  hl

    ;; ARP header

    ld   bc, #ARP_HEADER_SIZE
    push bc
    ld   hl, #arp_receive_reply_template
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; SHA: local MAC address

    ld   bc, #ETH_ADDRESS_SIZE
    push bc
    ld   hl, #_eth_local_address
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; SPA: local IPv4 address

    ld   c, #IPV4_ADDRESS_SIZE         ;; B==0 here
    push bc
    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; THA: sender MAC address

    ld   c, #ETH_ADDRESS_SIZE          ;; B==0 here
    push bc
    ld   hl, #_rx_eth_adm + ETH_ADM_OFFSETOF_SRC_ADDR
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; TPA: sender IP address, taken from SPA field in request

    ld   c, #IPV4_ADDRESS_SIZE         ;; B==0 here
    push bc
    ld   hl, #_rx_frame + ARP_OFFSET_SPA
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ld   c, #ARP_IP_ETH_PACKET_SIZE
    push bc
    call _eth_send
    pop  bc

    ret

arp_receive_reply_template::
    .db  0, ETH_HWTYPE         ;; HTYPE: 16 bits, network order
    .db  8, 0                  ;; PTYPE: ETHERTYPE_IP, 16 bits, network order
    .db  ETH_ADDRESS_SIZE      ;; HLEN (Ethernet)
    .db  IPV4_ADDRESS_SIZE     ;; PLEN (IPv4)
    .db  0, 2                  ;; OPER: reply, 16 bits, network order

  __endasm;
}
