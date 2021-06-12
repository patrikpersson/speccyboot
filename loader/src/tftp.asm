;; Module tftp:
;;
;; Trivial File Transfer Protocol (TFTP, RFC 1350)
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

    .module tftp

    .include "tftp.inc"

    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================

    .area _DATA

_expected_tftp_block_no:
    .ds 2        ;; next TFTP block we expect to receive

_server_port:
    .ds 2        ;; source port currently used by server

_tftp_client_port:
   .ds   2

_tftp_write_pos:
   .ds   2

;; ----------------------------------------------------------------------------
;; If non-NULL, this function is called for every received TFTP packet
;; (instead of regular raw data file handling)
;; ----------------------------------------------------------------------------

_tftp_receive_hook:
   .ds   2

;; ============================================================================

    .area _CODE

    ;; ------------------------------------------------------------------------
    ;; ACK is two bytes: 0, 4
    ;; ERROR is five bytes: 0, 5, 0, 4, 0
    ;; ------------------------------------------------------------------------

tftp_default_file:
    .ascii 'menu.bin'             ;; trailing NUL pinched from following packet
tftp_receive_error_packet:
    .db   0, TFTP_OPCODE_ERROR        ;; opcode in network order
tftp_receive_ack_opcode:
    .db   0, 4                        ;; illegal TFTP operation, network order
    .db   0                           ;; no particular message

;; ############################################################################
;; tftp_read_request
;; ############################################################################

tftp_read_request:

    push hl

    ;; ------------------------------------------------------------------------
    ;; reset _expected_tftp_block_no to 1
    ;; ------------------------------------------------------------------------

    ld   hl, #1
    ld   (_expected_tftp_block_no), hl

    ;; ------------------------------------------------------------------------
    ;; create UDP packet
    ;; ------------------------------------------------------------------------

    ;; set UDP ports

    ld   hl, #UDP_PORT_TFTP_SERVER * 0x0100    ;; network order
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl
    ld   a, r
    ld   l, a  ;; H is still UDP_PORT_TFTP_SERVER, so port number will not be zero
    ld   (_tftp_client_port), hl
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ;; calculate length of filename

    pop  hl
    push hl        ;; remember filename pointer for later

    xor  a
    ld   d, a
    ld   e, a
00001$:
    inc  de
    cpi
    jr   nz, 00001$

    push de        ;; remember filename length for later

    ld   hl, #UDP_HEADER_SIZE + (tftp_rrq_prefix_end - tftp_rrq_prefix) + TFTP_SIZE_OF_RRQ_OPTION
    add  hl, de
    ex   de, hl    ;; DE = UDP length

    ld   bc, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   hl, #eth_broadcast_address    ;; all we know at this point
    call udp_create

    ;; append 16-bit TFTP opcode

    ld   e, #tftp_rrq_prefix_end - tftp_rrq_prefix
    ld   hl, #tftp_rrq_prefix
    rst  enc28j60_write_memory_small

    ;; filename and length already stacked above

    pop  de
    pop  hl

    rst  enc28j60_write_memory_small

    ;; append option ("octet" mode)

    ld   hl, #tftp_rrq_option
    ld   e, #TFTP_SIZE_OF_RRQ_OPTION
    rst  enc28j60_write_memory_small

    jp   ip_send

    ;; ------------------------------------------------------------------------
    ;; constant data for outgoing TFTP packets
    ;; ------------------------------------------------------------------------

tftp_rrq_option:
    .ascii "octet"             ;; trailing NUL pinched from following packet
tftp_rrq_prefix:
    .db  0, TFTP_OPCODE_RRQ    ;; opcode in network order
    .ascii "speccyboot/"       ;; no NUL necessary here
tftp_rrq_prefix_end: