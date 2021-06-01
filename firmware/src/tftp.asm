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
    .optsdcc -mz80

    .include "tftp.inc"

    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "udp_ip.inc"
    .include "ui.inc"
    .include "util.inc"

;; ============================================================================
;; TFTP opcodes
;; ============================================================================

TFTP_OPCODE_RRQ           = 1
TFTP_OPCODE_DATA          = 3
TFTP_OPCODE_ACK           = 4
TFTP_OPCODE_ERROR         = 5

;; ============================================================================
;; Sizes and offsets of individual fields
;; ============================================================================

TFTP_SIZE_OF_OPCODE       = 2
TFTP_OFFSET_OF_OPCODE     = 0

TFTP_OFFSET_OF_BLOCKNO    = 2
TFTP_OFFSET_OF_ERROR_MSG  = 4

TFTP_SIZE_OF_RRQ_PREFIX   = 13

TFTP_SIZE_OF_RRQ_OPTION   = 6

;; ============================================================================
;; Packet sizes
;; ============================================================================

TFTP_SIZE_OF_ACK_PACKET   = 4
TFTP_SIZE_OF_ERROR_PACKET = 5

;; ============================================================================

    .area _DATA

_expected_tftp_block_no:
    .ds 2        ;; next TFTP block we expect to receive

_server_port:
    .ds 2        ;; source port currently used by server

    .area _CODE

;; ############################################################################
;; tftp_receive
;; ############################################################################

tftp_receive:

    ;; ------------------------------------------------------------------------
    ;; check destination port (should be _tftp_client_port)
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT
    ld   de, #_tftp_client_port
    ld   b, #2
    rst  memory_compare
    ret  nz

    ;; ------------------------------------------------------------------------
    ;; only accept DATA packets; anything else is a fatal error
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_OPCODE
    xor  a, a
    or   a, (hl)
    inc  hl
    jr   nz, tftp_receive_bad_reply
    ld   a, (hl)
    cp   a, #TFTP_OPCODE_DATA
    jr   z, tftp_receive_got_data

    ;; the only conceivable message type is now TFTP_OPCODE_ERROR

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_ERROR_MSG
    ld   de, #TFTP_VRAM_ERROR_POS
    call print_str

tftp_receive_bad_reply:

    ld   a, #FATAL_FILE_NOT_FOUND
    jp   fail

tftp_receive_got_data:

    ;; ========================================================================
    ;; check block number: acceptable cases are
    ;; received == expected   (normal case: acknowledge and read)
    ;; received == expected-1 (previous ACK lost: acknowledge, but do not read)
    ;; ========================================================================

    inc   hl
    ld    d, (hl)    ;; stored in network order, so read D first
    inc   hl
    ld    e, (hl)    ;; DE is now the received block number, in host order

    ld    hl, (_expected_tftp_block_no)

    ;; ------------------------------------------------------------------------
    ;; keep server-side port number in BC, and received block number in DE & IX
    ;; ------------------------------------------------------------------------

    ld    bc, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)
    push  de
    pop   ix

    ;; ------------------------------------------------------------------------
    ;; special case: if received and expected both are 1,
    ;; remember server port, and skip some checks
    ;; ------------------------------------------------------------------------

    ld    a, h        ;; are these
    or    a, d        ;; both zero?
    jr    nz, tftp_receive_check_blk_nbr   ;; if not, continue checking
    ld    a, e        ;; are these
    cp    a, l        ;; both the same value (specifically, 1) ?
    jr    nz, tftp_receive_check_blk_nbr   ;; if not, continue checking
    dec   a           ;; is A now 1?
    jr    nz, tftp_receive_check_blk_nbr   ;; if not, continue checking

    ;; this is the first packet, so note server port and accept the packet

    ld    (_server_port), bc
    jr    tftp_receive_blk_nbr_and_port_ok

tftp_receive_check_blk_nbr:

    ;; ------------------------------------------------------------------------
    ;; received == expected ?
    ;; ------------------------------------------------------------------------

    or    a, a   ;; clear C flag
    sbc   hl, de
    jr    z, tftp_receive_blk_nbr_ok

    ;; ------------------------------------------------------------------------
    ;; received == expected-1 ?   means HL-DE == expected-received == 1
    ;; ------------------------------------------------------------------------

    dec   hl
    ld    a, h
    or    a, l
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_ok:

    ;; ------------------------------------------------------------------------
    ;; check server port number:
    ;; must be the same as used for first packet
    ;; ------------------------------------------------------------------------

    ld    hl, (_server_port)
    ;; BC was loaded above, early on; holds server-side port nbr from packet
    or    a, a     ;; clear C flag
    sbc   hl, bc
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_and_port_ok:

    ;; ========================================================================
    ;; reply with ACK packet
    ;; ========================================================================

    ld    de, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET
    call  udp_reply

    ld    e, #TFTP_SIZE_OF_OPCODE
    ld    hl, #tftp_receive_ack_opcode
    rst   enc28j60_write_memory_small

    ld    e, #TFTP_SIZE_OF_OPCODE
    ld    hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO
    rst   enc28j60_write_memory_small

    call  ip_send

    ;; ========================================================================
    ;; if expected block nbr == received, increase expected and accept the data
    ;; ========================================================================

    ld    hl, (_expected_tftp_block_no)
    ld    d, h
    ld    e, l

    push  ix
    pop   bc
    or   a, a       ;; clear C flag
    sbc   hl, bc
    ret   nz

    inc   de
    ld    (_expected_tftp_block_no), de

    ;; ------------------------------------------------------------------------
    ;; If a receive hook has been installed, jump there
    ;; ------------------------------------------------------------------------

    ld  hl, (_tftp_receive_hook)
    ld  a, h
    or  l
    jr  z, 00002$
    jp  (hl)

00002$:

    ;; -----------------------------------------------------------------------
    ;; Compute TFTP data length by subtracting UDP+TFTP header sizes
    ;; from the UDP length. Start with the low-order byte in network order;
    ;; hence the "+1" below.
    ;; -----------------------------------------------------------------------

breakpoint::

    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH + 1
    ld  a, (hl)                                      ;; UDP length, low byte
    sub a, #(UDP_HEADER_SIZE + TFTP_HEADER_SIZE)  
    ld  c, a                                         ;; TFTP length, low byte
    dec hl
    ld  a, (hl)                                      ;; UDP length, high byte
    sbc a, #0
    ld  b, a                                         ;; TFTP length, high byte
 
    ;; BC is now payload length excluding headers, 0..512

    ld  de, (_tftp_write_pos)
    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ldir
    ld  (_tftp_write_pos), de

    ;; ------------------------------------------------------------------------
    ;; If a full TFTP packet was loaded, return.
    ;; (BC above should be exactly 0x200 for all DATA packets except the last
    ;; one, so we are done if A != 2 here)
    ;; ------------------------------------------------------------------------

    cp  a, #2
    ret z

    ;; ========================================================================
    ;; This was the last packet, of either the stage 2 binary or snapshots.lst.
    ;; Check version signature and execute the stage 2 loader.
    ;; ========================================================================

    ex   de, hl
    ld   (hl), #0                   ;; ensure loaded data is NUL-terminated

    ld  hl, #stage2_start
    ld  a, (hl)
    cp  a, #<VERSION_MAGIC
    jr  nz, version_mismatch
    inc hl
    ld  a, (hl)
    cp  a, #>VERSION_MAGIC
version_mismatch:
    ld  a, #FATAL_VERSION_MISMATCH
    jp  nz, version_mismatch
    inc hl
    jp  (hl)

tftp_receive_error:

    ld    de, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET
    call  udp_reply

    ld    e, #TFTP_SIZE_OF_ERROR_PACKET
    ld    hl, #tftp_receive_error_packet
    rst   enc28j60_write_memory_small

    jp    ip_send

    ;; ------------------------------------------------------------------------
    ;; ACK is two bytes: 0, 4
    ;; ERROR is five bytes: 0, 5, 0, 4, 0
    ;; ------------------------------------------------------------------------

tftp_default_file:
    .ascii 'spboot.bin'           ;; trailing NUL pinched from following packet
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

    ld   de, #TFTP_VRAM_FILENAME_POS
    call print_str

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

    ld   hl, #UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    add  hl, de
    ex   de, hl    ;; DE = UDP length

    ld   bc, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   hl, #eth_broadcast_address    ;; all we know at this point
    call udp_create

    ;; append 16-bit TFTP opcode

    ld   e, #TFTP_SIZE_OF_RRQ_PREFIX
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
