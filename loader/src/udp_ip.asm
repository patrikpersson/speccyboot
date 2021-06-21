;; Module udp_ip:
;;
;; User Datagram Protocol (UDP, RFC 768)
;; Internet Protocol (IP, RFC 791)
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

    .module udp_ip

    .include "udp_ip.inc"

    .include "bootp.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "tftp.inc"
    .include "util.inc"

;; ============================================================================

    .area _DATA

_ip_checksum:
    .ds   2

    .area _CODE

;; ############################################################################
;; ip_receive
;; ############################################################################

ip_receive:

    ;; clear IP checksum

    ld   hl, #0
    ld   (_ip_checksum), hl

    ;; read a minimal IPv4 header

    call enc28j60_read_20b_to_rxframe

    ;; ------------------------------------------------------------
    ;; Check the IP destination address
    ;; ------------------------------------------------------------

    ;; Check if a valid IP address has been set. Set Z as follows:
    ;;
    ;; Z == 0: an IP address has been set, check packet IP address
    ;; Z == 1: no IP address has been set, ignore packet IP address

    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld   a, (hl)                        ;; a non-zero first octet
    or   a                              ;; means an address has been set

    ;; This means that once an IP address is set,
    ;; multicasts/broadcasts are ignored.

    ld   de, #_rx_frame + IPV4_HEADER_OFFSETOF_DST_ADDR
    call nz, memory_compare_4_bytes
    ret  nz

ip_receive_address_checked:

    ;; ------------------------------------------------------------
    ;; Read remaining IP header, skip any options
    ;; ------------------------------------------------------------

    ;; Read header size

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH)
    and  a, #0x0f
    add  a, a
    add  a, a
    push af     ;; remember IP header size for later, carry == 0

    sub  a, #IPV4_HEADER_SIZE

    ;; -----------------------------------------------------------------------
    ;; Handle IPv4 options, if any. Z==0 means IPv4 options found.
    ;; -----------------------------------------------------------------------

    ;; To skip forward past any options, load additional header data
    ;; into UDP part of the buffer (overwritten soon afterwards)

    ld   d, #0
    ld   e, a
    ld   l, #<_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call nz, enc28j60_read_memory

    ;; B == 0 here, either from
    ;; enc28j60_read_20b_to_rxframe or memory_compare_4_bytes

    pop  af    ;; A now holds IP header size, carry == 0

    ;; ------------------------------------------------------------
    ;; Load UDP payload
    ;; ------------------------------------------------------------

    ld   c, a        ;; BC now holds IP header size

    ;; compute T-N, where
    ;;   T is the total packet length
    ;;   N is the number of bytes currently read (IP header + options)

    ld   hl, (_rx_frame + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH)
    ld   a, l
    ld   l, h
    ld   h, a        ;; byteswap from network to host order
    sbc  hl, bc      ;; carry is 0 from POP AF above

    ex   de, hl      ;; DE now holds UDP length

    ;; ------------------------------------------------------------
    ;; if IP header checksum is ok, load packet data
    ;; ------------------------------------------------------------

    call ip_receive_check_checksum

    ;; ------------------------------------------------------------
    ;; Check for UDP (everything else will be ignored)
    ;; ------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_PROT)
    cp   a, #IP_PROTOCOL_UDP
    ret  nz

    ;; ------------------------------------------------------------
    ;; Initialize IP checksum to IP_PROTOCOL_UDP + UDP length
    ;; (network order) for pseudo header. One assumption is made:
    ;;
    ;; - The UDP length is assumed to equal IP length - IP header
    ;;   size. This should certainly be true in general, but
    ;;   perhaps a creative IP stack could break this assumption.
    ;;   It _seems_ to work fine, though...
    ;; ------------------------------------------------------------

    add  a, e
    ld   h, a
    ld   l, d
    jr   nc, no_carry
    inc  l
no_carry:
    ld   (_ip_checksum), hl

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call enc28j60_read_memory

    ;; ------------------------------------------------------------
    ;; Check UDP checksum
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_CHECKSUM)
    ld   a, h
    or   l

    ;; UDP checksum is optional: Z flag is set if no UDP checksum given

    ;; Include IPv4 pseudo header in UDP checksum. UDP protocol and length
    ;; were already included (given as initial value above), so we do not add
    ;; it here.

    ld   b, #IPV4_ADDRESS_SIZE    ;; number of words (4 for two IP addresses)
    ld   de, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    call nz, add_and_verify_checksum

ip_receive_udp_checksum_done:

    ;; ------------------------------------------------------------
    ;; Pass on to BOOTP/TFTP
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)

    ;; BOOTP or TFTP response?
    ;;
    ;; for BOOTP, the port has the value 0x4400
    ;; (UDP_PORT_BOOTP_CLIENT, network order)
    ;;
    ;; for TFTP, port has the value 0x45rr, where 'rr' is a random value
    ;; (that is, the high byte is UDP_PORT_TFTP_SERVER, as chosen in tftp.inc)

    ld   a, h
    sub  a, #UDP_PORT_BOOTP_CLIENT
    jr   nz, ip_receive_not_bootp
    or   a, l
    jp   z, bootp_receive
    ret

ip_receive_not_bootp:
    dec  a             ;; UDP_PORT_TFTP_SERVER?
    ret  nz
    ld   a, (_tftp_client_port)
    cp   a, l
    ret  nz

    ;; only accept TFTP if an IP address has been set

    ld   a, (_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET)
    or   a  ;; a non-zero first octet
    ret  z

    ;; -------------------------------------------------------------------
    ;; handle_tftp_packet is a macro, so as to avoid a function call
    ;; -------------------------------------------------------------------

    handle_tftp_packet


;; -----------------------------------------------------------------------
;; Add a number of bytes to IP checksum, then verify the resulting
;; checksum.
;; -----------------------------------------------------------------------

add_and_verify_checksum:

    call enc28j60_add_to_checksum

    ;; FALL THROUGH to ip_receive_check_checksum

;; -----------------------------------------------------------------------
;; Helper: check IP checksum.
;; If OK (0xffff): return to caller.
;; if not OK: pop return address and return to next caller
;;            (that is, return from ip_receive)
;; Must NOT have anything else on stack when this is called.
;; Destroys AF, HL if OK; more if not.
;; -----------------------------------------------------------------------

ip_receive_check_checksum:
    ld   hl, (_ip_checksum)
    ld   a, h
    and  a, l
    inc  a   ;; if both bytes are 0xff, A will now become zero
    ret  z

    pop  af   ;; pop return address within ip_receive
    ret       ;; return to _caller_ of ip_receive


;; ############################################################################
;; Create UDP reply to the sender of the received packet currently processed.
;; Source/destination ports are swapped.
;;
;; Call with DE=number of bytes in payload
;; ############################################################################

udp_reply:

    ld   bc, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)
    ld   (_header_template  + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl
    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)
    ld   (_header_template  + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ld   hl, #eth_sender_address

    ;; FALL THROUGH to udp_create


;; ############################################################################
;; _udp_create
;; ############################################################################

udp_create:

    push  hl
    push  de
    push  bc

    ;; ----------------------------------------------------------------------
    ;; Set up a header template, to be filled in with proper data below.
    ;; ----------------------------------------------------------------------

    exx                  ;; remember DE for use below

    ld    hl, #ip_header_defaults
    ld    de, #_header_template
    ld    bc, #12         ;; IP v4 header size excluding src/dst addresses

    ldir

    exx                  ;; recall DE

    ;; ----------------------------------------------------------------------
    ;; Add IPV4_HEADER_SIZE to DE. This can safely be done as a byte addition
    ;; (no carry needed), as DE has one of the following values:
    ;;
    ;; BOOTP boot request: UDP_HEADER_SIZE + BOOTP_PACKET_SIZE = 308 = 0x134
    ;; TFTP read request: UDP_HEADER_SIZE
    ;;                       + TFTP_SIZE_OF_RRQ_PREFIX
    ;;                       + TFTP_SIZE_OF_RRQ_OPTION
    ;;                    = 27 = 0x1b
    ;; TFTP ACK: UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET = 8 + 4 = 0x0c
    ;; TFTP ERROR: UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET = 8 + 5 = 0x0d
    ;;
    ;; In all these cases, the lower byte (that is, E) is < 0xfc, so adding
    ;; IPV4_HEADER_SIZE = 20 = 0x14 as a byte addition is safe.
    ;; ----------------------------------------------------------------------

    ld    a, e
    add   a, #IPV4_HEADER_SIZE
    ld    e, a                 ;; DE is now total length, including IP header

    ;; ----------------------------------------------------------------------
    ;; prepare IP header in _header_template
    ;; ----------------------------------------------------------------------

    ld    hl, #_header_template + 2    ;; total length
    ld    (hl), d       ;; total_length  (network order)
    inc   hl
    ld    (hl), e       ;; total_length, continued

    ;; copy source IP address

    ld    de, #_header_template + 12   ;; source IP address
    ld    l, #<_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld    bc, #4
    ldir

    ;; copy destination IP address

    pop   hl
    ld    c, #4       ;; B == 0 after LDIR above
    ;; keep DE from above: destination address follows immediately after source
    ldir

    ;; ----------------------------------------------------------------------
    ;; compute checksum of IP header
    ;; ----------------------------------------------------------------------

    ld     h, b   ;; BC==0 here after LDIR above
    ld     l, c

    ld     b, #(IPV4_HEADER_SIZE / 2)   ;; number of words (10)
    ld     de, #_header_template
    call   enc28j60_add_to_checksum_hl

    ld     e, #<_header_template + IPV4_HEADER_OFFSETOF_CHECKSUM
    ld     a, l
    cpl
    ld     (de), a
    inc    de
    ld     a, h
    cpl
    ld     (de), a

    ;; ----------------------------------------------------------------------
    ;; set UDP length (network order) and clear UDP checksum
    ;; ----------------------------------------------------------------------

    pop    de       ;; UDP length

    ld     hl, #_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH
    ld     (hl), d
    inc    hl
    ld     (hl), e
    inc    hl

    xor    a, a
    ld     (hl), a
    inc    hl
    ld     (hl), a

    ;; ----------------------------------------------------------------------
    ;; create IP packet
    ;; ----------------------------------------------------------------------

    pop    bc             ;; destination MAC address
    ld     de, #ethertype_ip
    ld     hl, #ENC28J60_TXBUF1_START
    call   eth_create

    ;; ----------------------------------------------------------------------
    ;; call enc28j60_write_memory_cont(&header_template, sizeof(header_template));
    ;; ----------------------------------------------------------------------

    ld     e, #IPV4_HEADER_SIZE + UDP_HEADER_SIZE
    ld     hl, #_header_template
    rst    enc28j60_write_memory_small
    
    ret

;; ============================================================================
;; IP header defaults
;; https://en.wikipedia.org/wiki/IPv4#Header
;;
;; Four of these bytes are also used as BOOTREQUEST data. The length field
;; is only a placeholder (the actual value is set at runtime). The ID field
;; is arbitrary when the Don't Fragment flag is set, according to RFC 6864:
;;
;;   Originating sources MAY set the IPv4 ID field of
;;   atomic datagrams to any value.
;;
;; https://datatracker.ietf.org/doc/html/rfc6864#section-4.1
;;
;; The BOOTP XID is similarly arbitrary, and happens to be taken from the
;; flags, fragment offset, time-to-live, and protocol IP fields.
;; ============================================================================

ip_header_defaults:
    .db   0x45, 0            ;; version, IHL, DSCP, EN

bootrequest_header_data:
    .db   BOOTREQUEST, 1     ;; IP: length      BOOTP: op, htype (10M Ethernet)
    .db   6, 0               ;; IP: packet ID   BOOTP: hlen, hops

bootrequest_xid:
    .db   0x40, 0            ;; DO NOT FRAGMENT, fragment offset 0
    .db   0x40               ;; time to live
    .db   IP_PROTOCOL_UDP    ;; protocol

    .dw   0                  ;; checksum (temporary value for computation)
