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

void
ip_receive(void)
__naked
{
  __asm

    ;; clear IP checksum

    ld   hl, #0
    ld   (_ip_checksum), hl

    ;; read a minimal IPv4 header

    ld   de, #IPV4_HEADER_SIZE
    ld   hl, #_rx_frame
    call _enc28j60_read_memory_cont

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
    call _memory_compare

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
    push af     ;; remember IP header size for later

    sub  a, #IPV4_HEADER_SIZE
    jr   z, ip_receive_options_done

    ;; To skip forward past any options, load additional header data
    ;; into UDP part of the buffer (overwritten soon afterwards)

    ld   d, #0
    ld   e, a
    ld   hl, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call _enc28j60_read_memory_cont

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
    ld   (_ip_checksum), hl

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

    ex   de, hl
    ld   hl, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call _enc28j60_read_memory_cont

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
    ld   iy, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    call _enc28j60_add_checksum

    ld   c, #1 ;; one word, B==0 here
    ld   iy, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH
    call _enc28j60_add_checksum

    call ip_receive_check_checksum

ip_receive_udp_checksum_done::

    ;; ------------------------------------------------------------
    ;; Pass on to BOOTP/TFTP
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

    ;; BOOTP response?

    ld   a, e
    or   a
    ret  nz
    ld   a, d
    cp   a, #UDP_PORT_BOOTP_CLIENT
    jp   z, _bootp_receive
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
    ld   hl, (_ip_checksum)
    ld   a, h
    and  a, l
    inc  a   ;; if both bytes are 0xff, A will now become zero
    ret  z

    pop  af   ;; pop return address within ip_receive
    ret       ;; return to _caller_ of ip_receive

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
udp_create_impl(const struct mac_address_t  *dst_hwaddr,
                const ipv4_address_t        *dst_ipaddr,
                uint16_t                     udp_length)
__naked
{
  (void) dst_hwaddr, dst_ipaddr, udp_length;

  __asm

    push  ix

    ld    ix, #4
    add   ix, sp

    ;; assume
    ;; dst_hwaddr  at (ix + 0)
    ;; dst_ipaddr  at (ix + 2)
    ;; udp_length  at (ix + 4)

    ;; ----------------------------------------------------------------------
    ;; set up a header template, to be filled in with proper data below
    ;; ----------------------------------------------------------------------

    ld    hl, #ip_header_defaults
    ld    de, #_header_template
    ld    bc, #12         ;; IP v4 header size excluding src/dst addresses
    ldir

    ;; current_packet_length = udp_length + sizeof(struct ipv4_header_t);

    ld    l, 4(ix)
    ld    h, 5(ix)
    ld    c, #IPV4_HEADER_SIZE        ;; B is zero after LDIR above
    add   hl, bc

    ;; ----------------------------------------------------------------------
    ;; prepare IP header in _header_template
    ;; ----------------------------------------------------------------------

    ex    de, hl
    ld    hl, #_header_template + 2    ;; total length
    ld    (hl), d       ;; total_length  (network order)
    inc   hl
    ld    (hl), e       ;; total_length, continued

    ;; copy source IP address

    ld    de, #_header_template + 12   ;; source IP address
    ld    hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld    c, #4   ;; B is zero after LDIR/add above
    ldir

    ;; copy destination IP address

    ld    l, 2(ix)
    ld    h, 3(ix)
    ld    c, #4
    ldir

    ;; ----------------------------------------------------------------------
    ;; compute checksum of IP header
    ;; ----------------------------------------------------------------------

    ld     h, b   ;; BC=0 here
    ld     l, c
    ld     (_ip_checksum), hl

    ld     c, #(IPV4_HEADER_SIZE / 2)   ;; number of words (10); B=0 here
    ld     iy, #_header_template
    call   _enc28j60_add_checksum

    ld     hl, #_ip_checksum
    ld     de, #_header_template + IPV4_HEADER_OFFSETOF_CHECKSUM
    ld     a, (hl)
    cpl
    ld     (de), a
    inc    hl
    inc    de
    ld     a, (hl)
    cpl
    ld     (de), a

    ;; ----------------------------------------------------------------------
    ;; set UDP length (network order) and clear UDP checksum
    ;; ----------------------------------------------------------------------

    ld     hl, #_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH
    ld     a, 5(ix)
    ld     (hl), a
    inc    hl
    ld     a, 4(ix)
    ld     (hl), a
    inc    hl

    xor    a, a
    ld     (hl), a
    inc    hl
    ld     (hl), a

    ;; ----------------------------------------------------------------------
    ;; call eth_create(dst_hwaddr, htons(ETHERTYPE_IP), ENC28J60_TXBUF1_START)
    ;; ----------------------------------------------------------------------

    ld    l, 0(ix)
    ld    h, 1(ix)
    call  _eth_create    ;; A is zero (means IP), after XOR A, A above

    ;; ----------------------------------------------------------------------
    ;; call enc28j60_write_memory_cont(&header_template, sizeof(header_template));
    ;; ----------------------------------------------------------------------

    pop    ix

    ld     de, #IPV4_HEADER_SIZE + UDP_HEADER_SIZE
    ld     hl, #_header_template
    jp     _enc28j60_write_memory_cont

    ;; ----------------------------------------------------------------------
    ;; IP header defaults
    ;; https://en.wikipedia.org/wiki/IPv4#Header
    ;; ----------------------------------------------------------------------

ip_header_defaults::
    .db   0x45, 0            ;; version, IHL, DSCP, EN
    .dw   0xffff             ;; total length (to be replaced)
    .dw   0                  ;; identification
    .db   0x40, 0            ;; DO NOT FRAGMENT, fragment offset 0
    .db   0x40               ;; time to live
    .db   IP_PROTOCOL_UDP    ;; protocol
    .dw   0                  ;; checksum (temporary value for computation)

  __endasm;
}
