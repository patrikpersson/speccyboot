;; Module arp:
;;
;; Address Resolution Protocol (ARP, RFC 826)
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
;;
;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use,
;; copy, modify, merge, publish, distribute, sublicense, and/or sell
;; copies of the Software, and to permit persons to whom the
;; Software is furnished to do so, subject to the following
;; conditions:
;;
;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.
;;
;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;; OTHER DEALINGS IN THE SOFTWARE.

    .module arp
    .optsdcc -mz80

    .include "include/arp.inc"

    .include "include/eth.inc"
    .include "include/enc28j60.inc"
    .include "include/globals.inc"
    .include "include/udp_ip.inc"
    .include "include/util.inc"

;; ============================================================================
;; ARP header constants
;; ============================================================================

ARP_HEADER_SIZE = 8         ;; ARP header size
ARP_OFFSET_SPA = 14         ;; offset of SPA field in ARP header
ARP_OFFSET_TPA = 24         ;; offset of TPA field in ARP header
ARP_IP_ETH_PACKET_SIZE = 28 ;; size of an ARP packet for an IP-Ethernet mapping

    .area _CODE

;; ############################################################################
;; arp_receive
;;
;; Called by eth.c when an Ethernet frame holding an ARP packet has been
;; received.
;; ############################################################################

arp_receive:

    ;; ------------------------------------------------------------------------
    ;; retrieve ARP payload
    ;; ------------------------------------------------------------------------

    ld   de, #ARP_IP_ETH_PACKET_SIZE
    ld   hl, #_rx_frame
    call enc28j60_read_memory

    ;; ------------------------------------------------------------------------
    ;; check header against template
    ;; (ARP_OPER_REQUEST, ETHERTYPE_IP, ETH_HWTYPE)
    ;; ------------------------------------------------------------------------

    ;; first check everything except OPER

    ld   de, #arp_receive_reply_template
    ld   b, #(ARP_HEADER_SIZE - 1)
    rst  memory_compare
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
    rst  memory_compare
    ret  nz   ;; if the packet is not for the local IP address, return

    ;; ========================================================================
    ;; create ARP response
    ;; ========================================================================

    ld   hl, #eth_sender_address
    cpl       ;; A was zero after memory_compare, now 0xFF, non-zero means ARP
    call eth_create

    ;; ARP header

    ld   de, #ARP_HEADER_SIZE
    ld   hl, #arp_receive_reply_template
    call enc28j60_write_memory

    ;; SHA: local MAC address

    ld   hl, #eth_local_address
    call enc28j60_write_memory_6_bytes

    ;; SPA: local IPv4 address

    ld   e, #IPV4_ADDRESS_SIZE         ;; D==0 here
    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    call enc28j60_write_memory

    ;; THA: sender MAC address

    ld   hl, #eth_sender_address
    call enc28j60_write_memory_6_bytes

    ;; TPA: sender IP address, taken from SPA field in request

    ld   e, #IPV4_ADDRESS_SIZE         ;; D==0 here
    ld   hl, #_rx_frame + ARP_OFFSET_SPA
    call enc28j60_write_memory

    ld   hl, #ARP_IP_ETH_PACKET_SIZE
    jp   eth_send

arp_receive_reply_template:
    .db  0, ETH_HWTYPE         ;; HTYPE: 16 bits, network order
ethertype_ip:
    .db  8, 0                  ;; PTYPE: ETHERTYPE_IP, 16 bits, network order
    .db  ETH_ADDRESS_SIZE      ;; HLEN (Ethernet)
    .db  IPV4_ADDRESS_SIZE     ;; PLEN (IPv4)
    .db  0, 2                  ;; OPER: reply, 16 bits, network order
