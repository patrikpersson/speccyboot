;;
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
;;

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

TFTP_SIZE_OF_BLOCKNO      = 2
TFTP_OFFSET_OF_BLOCKNO    = 2
TFTP_OFFSET_OF_ERROR_MSG  = 4

TFTP_SIZE_OF_RRQ_PREFIX   = 2
TFTP_SIZE_OF_RRQ_OPTION   = 6

;; ============================================================================
;; Packet sizes
;; ============================================================================

TFTP_SIZE_OF_ACK_PACKET   = 4
TFTP_SIZE_OF_ERROR_PACKET = 5

;; ----------------------------------------------------------------------------
;; TFTP DATA packets have a maximal size of 512 bytes, unless options are set
;; by the client (and this client won't)
;; ----------------------------------------------------------------------------

TFTP_DATA_MAXSIZE    = 512

;; ----------------------------------------------------------------------------
;; TFTP packets
;; ----------------------------------------------------------------------------

TFTP_HEADER_SIZE     = 4

;; ----------------------------------------------------------------------------
;; Position to write loaded TFTP data to (initialized in init.asm)
;; ----------------------------------------------------------------------------

    .globl _tftp_write_pos

;; ----------------------------------------------------------------------------
;; Request snapshot to be loaded over TFTP. DE points to .z80 file name.
;; ----------------------------------------------------------------------------

    .globl tftp_request_snapshot


;; ============================================================================
;; Macro: executed by UDP when a TFTP packet has been identified.
;; Returns (RET or RET cc) when done.
;; ============================================================================

    .macro  HANDLE_TFTP_PACKET

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
tftp_receive_bad_reply:
    ld   a, #FATAL_FILE_NOT_FOUND
    jp   nz, fail

tftp_receive_got_data:

    ;; ------------------------------------------------------------------------
    ;; keep server-side port number in BC
    ;; ------------------------------------------------------------------------

    ld    bc, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)

    ;; ========================================================================
    ;; check block number: acceptable cases are
    ;; received == expected   (normal case: acknowledge and read)
    ;; received == expected-1 (previous ACK lost: acknowledge, but do not read)
    ;;
    ;; only the least significant byte is checked
    ;; ========================================================================

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO + 1)
    ld    hl, #_expected_tftp_block_no

    ;; ------------------------------------------------------------------------
    ;; special case: if received and expected both are 1,
    ;; remember server port, and skip some checks
    ;; ------------------------------------------------------------------------

    cp    a, (hl)     ;; both the same value?
    jr    nz, tftp_receive_check_blk_nbr   ;; if not, continue checking
    cp    a, #1
    jr    nz, tftp_receive_blk_nbr_ok

    ;; this is the first packet, so note server port and accept the packet

    ld    (_server_port), bc
    jr    tftp_receive_blk_nbr_and_port_ok

tftp_receive_check_blk_nbr:

    ;; ------------------------------------------------------------------------
    ;; received == expected-1 ?  means A-(HL) == received-expected == -1 / 0xff
    ;; ------------------------------------------------------------------------

    sub   a, (hl)
    inc   a
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_ok:

    ;; ------------------------------------------------------------------------
    ;; check server port number:
    ;; must be the same as used for first packet
    ;; ------------------------------------------------------------------------

    ld    hl, (_server_port)
    ;; BC was loaded above, early on; holds server-side port nbr from packet
    or    a, a               ;; clear carry
    sbc   hl, bc
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_and_port_ok:

    ;; ========================================================================
    ;; reply with ACK packet
    ;; ========================================================================

    ld    de, #(UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET) * 0x0100 ;; network order
    call  tftp_reply

    rst   enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline
    ;; -----------------------------------------------------------------------

    .db  ack_packet_end - ack_packet_start

ack_packet_start:
    .db   0, TFTP_OPCODE_ACK
ack_packet_end:

    ;; -----------------------------------------------------------------------

    ld    e, #TFTP_SIZE_OF_BLOCKNO
    ld    hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO
    rst   enc28j60_write_memory_small

    call  ip_send

    ;; ========================================================================
    ;; if expected block nbr == received, increase expected and accept the data
    ;; ========================================================================

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO + 1)
    ld    hl, #_expected_tftp_block_no

    cp    a, (hl)
    ret   nz

    inc   (hl)

    ;; -----------------------------------------------------------------------
    ;; Compute TFTP data length by subtracting UDP+TFTP header sizes
    ;; from the UDP length. Start with the low-order byte in network order.
    ;; Set BC to TFTP payload length, 0..512.
    ;; -----------------------------------------------------------------------

    ld  hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH)
    ld  a, h                                           ;; UDP length, low byte
    sub a, #(UDP_HEADER_SIZE + TFTP_HEADER_SIZE)
    ld  c, a                                          ;; TFTP length, low byte
    ld  b, l                                         ;; TFTP length, high byte
    jr  nc, no_carry_in_tftp_length
    dec b
no_carry_in_tftp_length:

    ;; ------------------------------------------------------------------------
    ;; Set up remaining registers for TFTP state handlers:
    ;;
    ;; BC: number of bytes left to read from TFTP packet (already set above)
    ;; DE: write pointer (destination somewhere in RAM)
    ;; HL: number of bytes remaining in current chunk
    ;; IX: current state
    ;; IY: read pointer (TFTP packet data)
    ;; I:  number of ED ED repetitions remaining (set in z80_loader)
    ;; ------------------------------------------------------------------------

    ld   hl, (_chunk_bytes_remaining)
    ld   de, (_tftp_write_pos)

    ld   ix, (tftp_state)
    ld   iy, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE

    ;; ========================================================================
    ;; read bytes, call state functions
    ;; ========================================================================

tftp_state_loop:

    ;; ------------------------------------------------------------------------
    ;; if BC is zero, we are done (no more data in packet)
    ;; ------------------------------------------------------------------------

    ld    a, b
    or    a, c
    ret   z

    ;; ------------------------------------------------------------------------
    ;; call function pointed to by z80_loader_state
    ;; there is no "CALL (IX)" instruction, so CALL a JP (IX) instead
    ;; ------------------------------------------------------------------------

    call  jp_ix_instr

    ld    (tftp_state), ix
    ld    (_chunk_bytes_remaining), hl
    ld    (_tftp_write_pos), de

    jr    tftp_state_loop

tftp_receive_error:

    ld    de, #(UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET) * 0x0100 ;; network order
    call  tftp_reply

    rst   enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline
    ;; -----------------------------------------------------------------------

    .db  error_packet_end - error_packet_start

error_packet_start:
    .db   0, TFTP_OPCODE_ERROR        ;; opcode in network order
    .db   0, 4                        ;; illegal TFTP operation, network order
    .db   0                           ;; no particular message
error_packet_end:

    jr    ip_send

    .endm



;; ============================================================================
;; Macro: prepare (but do not send) a TFTP read request packet.
;; DE: pointer to file name
;; HL: pointer to TFTP state handler
;;
;; B==0 on exit.
;; ============================================================================

    .macro PREPARE_TFTP_READ_REQUEST

    ;; ------------------------------------------------------------------------
    ;; Set initial state for TFTP data handling
    ;; ------------------------------------------------------------------------

    ld   (tftp_state), hl

    ;; ========================================================================
    ;; create UDP packet
    ;; ========================================================================

    ;; set UDP ports

    ld   hl, #UDP_PORT_TFTP_SERVER * 0x0100    ;; network order
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl
    ld   a, r
    ld   (_tftp_client_port), a
    ld   l, a  ;; H is still UDP_PORT_TFTP_SERVER, so port number will not be zero
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ;; ------------------------------------------------------------------------
    ;; reset _expected_tftp_block_no to 1
    ;; ------------------------------------------------------------------------

    ld   a, #1
    ld   (_expected_tftp_block_no), a

    ex   de, hl    ;; bring filename pointer to HL
    push hl        ;; remember filename pointer for later

    ;; ------------------------------------------------------------------------
    ;; calculate length of filename; assume length < 240
    ;; (256 - UDP_HEADER_SIZE - TFTP_SIZE_OF_RRQ_PREFIX - TFTP_SIZE_OF_RRQ_OPTION)
    ;; ------------------------------------------------------------------------

    xor  a, a      ;; needed for CPIR, to search for NUL
    ld   c, a      ;; set C := 0
    cpir           ;; find NUL in string pointed to by HL; BC := negated length
    ld   b, a      ;; set B := 0
    ld   e, a      ;; set E := 0

    sub  a, c
    ld   c, a      ;; negate C

    push bc        ;; remember filename length for later

    ;; set DE to UDP length, in network order (least significant byte in D)
    ;; (E == 0 from above)

    add  a, #UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    ld   d, a

    ld   bc, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   hl, #eth_broadcast_address    ;; all we know at this point
    call udp_create

    ;; append 16-bit TFTP opcode and path prefix

    rst  enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline
    ;; -----------------------------------------------------------------------

    .db  rrq_prefix_end - rrq_prefix_start

rrq_prefix_start:
    .db  0, TFTP_OPCODE_RRQ    ;; opcode in network order
rrq_prefix_end:

    ;; -----------------------------------------------------------------------


    ;; filename and length already stacked above

    pop  de
    pop  hl

    rst  enc28j60_write_memory_small

    ;; append option ("octet" mode)

    rst  enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline
    ;; -----------------------------------------------------------------------

    .db  rrq_option_end - rrq_option_start

rrq_option_start:
    .ascii "octet"
    .db    0
rrq_option_end:

    .endm