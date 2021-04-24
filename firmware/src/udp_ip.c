/*
 * Module udp_ip:
 *
 * User Datagram Protocol (UDP, RFC 768)
 * Internet Protocol (IP, RFC 791)
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

#include "udp_ip.h"

#include "eth.h"
#include "globals.h"

/* ------------------------------------------------------------------------- */

/* Global IP configuration */
struct ip_config_t ip_config = {
  IP_DEFAULT_HOST_ADDRESS,
  IP_DEFAULT_BCAST_ADDRESS
  /* no value specified for TFTP server -- will be all zero */
};

/* Length of currently constructed TX UDP packet */
static uint16_t current_packet_length;

/* Definitions for state exposed in header */
PACKED_STRUCT(header_template_t) header_template;
uint16_t tftp_client_port = htons(0xc000);

/* ------------------------------------------------------------------------- */

void
ip_receive(void)
__naked
{
  __asm

    ;; clear IP checksum

    ld   hl, #0
    ld   (_enc28j60_ip_checksum), hl

    ;; read a minimal IPv4 header

    ld   l, #IPV4_HEADER_SIZE   ;; H is already zero here
    push hl
    ld   hl, #_rx_frame
    push hl
    call _enc28j60_read_memory_cont
    pop  af
    pop  af

    ;; ------------------------------------------------------------
    ;; Check the IP destination address
    ;; ------------------------------------------------------------

    ;; Check if a valid IP address has been set

    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld   a, (hl)
    or   a                              ;; a non-zero first octet
    jr   z, ip_receive_address_checked  ;; means no has been set

    ;; An IP address has been set. Is the packet sent to this address?
    ;; If it is not, return immediately.

    ;; This means that once an IP address is set,
    ;; multicasts/broadcasts are ignored.

    ld   de, #_rx_frame + IPV4_HEADER_OFFSETOF_DST_ADDR
    ld   b, #4
ip_receive_chk_addr_loop::
    ld   a, (de)
    cp   a, (hl)
    ret  nz
    inc  hl
    inc  de
    djnz ip_receive_chk_addr_loop

ip_receive_address_checked::

    ;; ------------------------------------------------------------
    ;; Check for UDP (everything else will be ignored)
    ;; ------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_PROT)
    cp   a, #IP_PROTOCOL_UDP
    ret  nz

    ;; ------------------------------------------------------------
    ;; Read IP header, skip any options
    ;; ------------------------------------------------------------

    ;; Read header size

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH)
    and  a, #0x0f
    add  a, a
    add  a, a
    push af     ;; remember for later

    sub  a, #IPV4_HEADER_SIZE
    jr   z, ip_receive_options_done

    ;; To skip forward past any options, load additional header data
    ;; into UDP part of the buffer (overwritten soon afterwards)

    ld   b, #0
    ld   c, a
    push bc
    ld   hl, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    push hl
    call _enc28j60_read_memory_cont
    pop  hl
    pop  bc

ip_receive_options_done::

    pop  bc    ;; B now holds IP header size

    ;; ------------------------------------------------------------
    ;; Check IP header checksum
    ;; ------------------------------------------------------------

    call ip_receive_check_checksum

    ;; ------------------------------------------------------------
    ;; Load UDP payload
    ;; ------------------------------------------------------------

    ;; Set IP checksum to htons(IP_PROTOCOL_UDP), for pseudo header
    ld   hl, #(IP_PROTOCOL_UDP << 8)   ;; network order is big-endian
    ld   (_enc28j60_ip_checksum), hl

    ;; compute T-N, where
    ;;   T is the total packet length
    ;;   N is the number of bytes currently read (IP header + options)

    ld   hl, (_rx_frame + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH)
    ld   a, l
    ld   l, h
    ld   h, a        ;; byteswap from network to host order

    xor  a, a        ;; clear C flag
    ld   c, b
    ld   b, a        ;; BC now holds IP header size
    sbc  hl, bc

    push hl
    ld   bc, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    push bc
    call _enc28j60_read_memory_cont
    pop  bc
    pop  hl

    ;; ------------------------------------------------------------
    ;; Check UDP checksum
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_CHECKSUM)
    ld   a, h
    or   l
    jr   z, ip_receive_udp_checksum_done   ;; UDP checksum is optional

    ;; Include IPv4 pseudo header in UDP checksum. The word for UDP protocol
    ;; was already included (given as initial value above), so we do not add
    ;; it here.

    ld   bc, #IPV4_ADDRESS_SIZE ;; BC is number of words (4)
    ld   hl, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    push bc
    push hl
    call _enc28j60_add_checksum
    pop  hl
    pop  bc

    ld   bc, #1 ;; one word
    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH
    push bc
    push hl
    call _enc28j60_add_checksum
    pop  hl
    pop  bc

    call ip_receive_check_checksum

ip_receive_udp_checksum_done::

    ;; ------------------------------------------------------------
    ;; Pass on to BOOTP/DHCP/TFTP
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)
    ex   de, hl

    ;; TFTP response?

    ld   hl, (_tftp_client_port)
    ld   a, h
    cp   a, d
    jr   nz, ip_receive_not_tftp
    ld   a, l
    cp   a, e
    jr   nz, ip_receive_not_tftp

    ;; only accept TFTP if an IP address has been set

    ld   a, (_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET)
    or   a  ;; a non-zero first octet
    jp   nz, _tftp_receive

ip_receive_not_tftp::

    ;; DHCP response?

    ld   a, e
    or   a
    ret  nz
    ld   a, d
    cp   a, #UDP_PORT_DHCP_CLIENT
#ifdef SB_MINIMAL
    jp   z, _bootp_receive
#else
    jp   z, _dhcp_receive
#endif
    ret

;; -----------------------------------------------------------------------
;; Helper: check IP checksum.
;; If OK (0xffff): return to caller.
;; if not OK: pop return address and return to next caller
;;            (that is, return from ip_receive)
;; Must NOT have anything else on stack when this is called.
;; Destroys AF, HL if OK; more if not.
;; -----------------------------------------------------------------------

ip_receive_check_checksum::
    ld   hl, (_enc28j60_ip_checksum)
    ld   a, h
    and  a, l
    inc  a   ;; if both bytes are 0xff, A will now become zero
    ret  z

    ld   hl, #ip_receive_bad_checksum
    push hl
    call	_syslog
    pop  af   ;; pop arg to syslog
    pop  af   ;; pop return address within ip_receive

    ret       ;; return to caller of ip_recieve

ip_receive_bad_checksum::
    .ascii "bad checksum"
    .db 0x00

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
udp_create_impl(const struct mac_address_t  *dst_hwaddr,
                const ipv4_address_t        *dst_ipaddr,
                uint16_t                     udp_length,
                enc28j60_addr_t              frame_class)
{
  current_packet_length = udp_length + sizeof(struct ipv4_header_t);

  header_template.ip.version_and_header_length = 0x45;
  header_template.ip.type_of_service = 0;
  header_template.ip.total_length    = htons(current_packet_length);
  header_template.ip.id_and_fraginfo = 0x00400000; /* don't fragment */
  header_template.ip.time_to_live    = 0x40;
  header_template.ip.prot            = IP_PROTOCOL_UDP;
  header_template.ip.checksum        = 0;  /* temporary value for computation */
  header_template.ip.src_addr        = ip_config.host_address;
  header_template.ip.dst_addr        = *dst_ipaddr;

  ip_checksum_set(0);
  ip_checksum_add(header_template.ip);
  header_template.ip.checksum        = ip_checksum_value();

  header_template.udp.length         = htons(udp_length);
  header_template.udp.checksum       = 0;

  eth_create(dst_hwaddr, htons(ETHERTYPE_IP), frame_class);
  eth_add(header_template);
}

/* ------------------------------------------------------------------------- */

void
udp_create_reply(uint16_t udp_payload_length, bool broadcast)
{
  udp_create(broadcast ? &eth_broadcast_address       : &rx_eth_adm.eth_header.src_addr,
             broadcast ? &ip_config.broadcast_address : &rx_frame.ip.src_addr,
             rx_frame.udp.header.dst_port,
             rx_frame.udp.header.src_port,
             udp_payload_length,
             ETH_FRAME_PRIORITY);
}

/* ------------------------------------------------------------------------- */

void
udp_send(void)
{
  eth_send(current_packet_length);
}
