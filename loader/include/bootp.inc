;; Module bootp:
;;
;; Boot Protocol (BOOTP, RFC 951)
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2021-  Patrik Persson
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

;; ============================================================================
;; BOOTP operations
;; ============================================================================

BOOTREQUEST            = 1
BOOTREPLY              = 2

;; ============================================================================
;; BOOTP packet structure
;; ============================================================================

BOOTP_PART1_SIZE       = 8
BOOTP_PART2_SIZE       = 20
BOOTP_PART3_SIZE       = 6
BOOTP_PART4_SIZE       = 266

BOOTP_PACKET_SIZE = (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + BOOTP_PART4_SIZE)

BOOTP_OFFSETOF_OP      = 0
BOOTP_OFFSETOF_XID     = 4
BOOTP_OFFSETOF_YIADDR  = BOOTP_PART1_SIZE + 8

BOOTP_OFFSETOF_SNAME = (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + 10)

BOOTP_OFFSETOF_FILE = (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + 10 + 64)


;; ----------------------------------------------------------------------------
;; Macro:
;;
;; send a BOOTREQUEST for client configuration
;; (IP address, boot file name, TFTP server address)
;; ----------------------------------------------------------------------------

    .macro bootp_init

    ;; ========================================================================
    ;; the BOOTREQUEST is built in steps:
    ;; - 8 constant-valued bytes
    ;; - 20 zero-valued bytes
    ;; - 6 bytes of SpeccyBoot Ethernet address
    ;; - 266 zero-valued bytes
    ;; Total: 300 bytes (BOOTP_PACKET_SIZE)
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; create UDP packet
    ;; ------------------------------------------------------------------------

    ld   a, #UDP_PORT_BOOTP_CLIENT
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT + 1), a
    dec  a  ;; UDP_PORT_BOOTP_SERVER = UDP_PORT_BOOTP_CLIENT - 1
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT + 1), a

    ld   de, #UDP_HEADER_SIZE + BOOTP_PACKET_SIZE
    ld   hl, #eth_broadcast_address
    ld   b, h     ;; works for IP broadcast too (4 x 0xff)
    ld   c, l
    call udp_create

    ;; ------------------------------------------------------------------------
    ;; part 1: 8 bytes of header (constants)
    ;; ------------------------------------------------------------------------

    ld   e, #BOOTP_PART1_SIZE
    ld   hl, #bootrequest_header_data
    rst  enc28j60_write_memory_small

    ;; ------------------------------------------------------------------------
    ;; part 2: 20 bytes of zeros
    ;; use VRAM as source of 20 zero-valued bytes
    ;; ------------------------------------------------------------------------

    ld   e, #BOOTP_PART2_SIZE
    ld   hl, #0x4800
    push hl
    rst  enc28j60_write_memory_small

    ;; ------------------------------------------------------------------------
    ;; part 3: 6 bytes of MAC address
    ;; ------------------------------------------------------------------------

    call enc28j60_write_local_hwaddr

    ;; ------------------------------------------------------------------------
    ;; part 4: 266 bytes of zeros
    ;; use VRAM as source of 266 zero-valued bytes
    ;; ------------------------------------------------------------------------

    ld   de, #BOOTP_PART4_SIZE
    pop  hl                       ;; HL is now 0x4800, zeros in VRAM
    call enc28j60_write_memory

    call ip_send

    .endm

;; ----------------------------------------------------------------------------
;; Macro:
;;
;; handle a received BOOTP packet
;; If a BOOTREPLY with an IP address is found,
;; fall through to tftp_request_snapshot; otherwise return.
;; ----------------------------------------------------------------------------


    .macro  HANDLE_BOOTP_PACKET

    ;; ------------------------------------------------------------------------
    ;; only accept BOOTREPLY packets with correct XID
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_OP)
    cp   a, #BOOTREPLY
    ld   hl, #bootrequest_xid
    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_XID
    call z, memory_compare_4_bytes
    ret  nz   ;; will return if (a) not a BOOTREPLY or (b) XID does not match

    ;; ------------------------------------------------------------------------
    ;; Copy two IP addresses (8 bytes, local + server address) from packet to
    ;; local IP configuration. This means that the TFTP server address will
    ;; default to the DHCP server address.
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_YIADDR
    ld   e, #<_ip_config
    ld   bc, #8
    ldir

    ;; ========================================================================
    ;; Check SNAME field for a dotted-decimal IP address (four octets)
    ;; ========================================================================

    ld   e, #<_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_SNAME
    ld   a, (de)
    or   a, a
    jr   z, bootp_receive_sname_done

    ld   l, #<_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   b, #4  ;; four octets

bootp_receive_octet_loop:
    push bc

    ;; ========================================================================
    ;; Parse decimal number at DE. Truncated to 8 bits (unsigned).
    ;; ========================================================================

    ld   c, #0

parse_loop:

    ld   a, (de)
    or   a, a
    jr   z, parse_byte_complete
    cp   a, #'.'
    jr   z, parse_byte_complete

    inc  de

    ;; backwards comparison, to ensure C is _cleared_ for non-digits
    add  a, #(0x100 - '0')
    jr   nc, parse_invalid_address

    cp   a, #10
    ld   b, a          ;; B now holds digit value 0..9
parse_invalid_address:
    ld   a, #FATAL_INVALID_BOOT_SERVER
    jr   nc, fail

    ld   a, c
    add  a, a
    add  a, a
    add  a, a
    add  a, c
    add  a, c
    add  a, b       ;; A := C*10 + B

    ld   c, a

    jr parse_loop

parse_byte_complete:

    ld   (hl), c
    pop  bc

    inc  hl

    ld   a, (de)
    inc  de
    or   a      ;; can only be '.' or NUL here
    jr   nz, bootp_receive_more_octets

    ;; DE apparently points to NUL. This is only OK after last octet (B==1)

    ld   a, b
    dec  a
    jr   nz, parse_invalid_address

bootp_receive_more_octets:
    djnz bootp_receive_octet_loop

    ;; If we got here, the last octet was apparently followed by a period.
    ;; This is technically wrong, but accepted.

bootp_receive_sname_done:

    ;; FALL THROUGH to tftp_request_snapshot

    .endm
