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
;;
;; Requires B == 0 on entry.
;; ============================================================================

    .macro  HANDLE_TFTP_PACKET

    ;; ------------------------------------------------------------------------
    ;; only accept DATA packets; anything else is a fatal error
    ;;
    ;; only check the least significant byte (network order)
    ;;
    ;; modify opcode to ACK, so the header can be used for acknowledgment
    ;; ------------------------------------------------------------------------

    ld   hl, #rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_OPCODE + 1
    ld   a, (hl)
    inc  (hl)                                      ;; modify to TFTP_OPCODE_ACK
    sub  a, #TFTP_OPCODE_DATA

    ;; ------------------------------------------------------------------------
    ;; The only conceivable non-DATA packet at this point would be an ERROR
    ;; packet. In this case, A is now
    ;; (TFTP_OPCODE_ERROR - TFTP_OPCODE_DATA)  ==  2  == FATAL_FILE_NOT_FOUND
    ;; ------------------------------------------------------------------------

    jp   nz, fail

    ;; ========================================================================
    ;; check block number: acceptable cases are
    ;;   received == expected     (normal case: acknowledge and read)
    ;;
    ;; A possible case is
    ;;   received == expected-1   (previous ACK lost)
    ;;
    ;; The latter case is ignored, so the previous ACK will be re-transmitted
    ;; upon time-out.
    ;;
    ;; The TFTP RFC indicates that an ERROR response is not critical:
    ;;
    ;; "This is only a courtesy since it will not be retransmitted or
    ;;  acknowledged, so it may never be received.
    ;;  Timeouts must also be used to detect errors."
    ;;
    ;; (https://datatracker.ietf.org/doc/html/rfc1350#section-7)
    ;;
    ;; No ERROR packet is sent here. Incorrect packets are silently dropped.
    ;; ========================================================================

    ld    a, (rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO + 1)
    dec   a
    ld    l, #<tftp_last_block_no_received

    cp    a, (hl)
    ret   nz

    ;; -----------------------------------------------------------------------
    ;; packet number OK: acknowledge
    ;; -----------------------------------------------------------------------

    inc   (hl)
    call  tftp_reply_ack                     ;; B == 0 from precondition above

    ;; -----------------------------------------------------------------------
    ;; Compute TFTP data length by subtracting UDP+TFTP header sizes
    ;; from the UDP length. Start with the low-order byte in network order.
    ;; Set BC to TFTP payload length, 0..512.
    ;; -----------------------------------------------------------------------

    ld  hl, (rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH)
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
    ;; ------------------------------------------------------------------------

    ld   hl, (_chunk_bytes_remaining)
    ld   de, (_tftp_write_pos)

    ld   ix, (tftp_state)
    ld   iy, #rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE

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

    .endm


;; ============================================================================
;; Macro: prepare and send a TFTP read request packet.
;; DE: pointer to file name
;; HL: pointer to TFTP state handler
;; ============================================================================

    .macro SEND_TFTP_READ_REQUEST

    ;; ------------------------------------------------------------------------
    ;; Set initial state for TFTP data handling
    ;; ------------------------------------------------------------------------

    ld   (tftp_state), hl

    ;; ========================================================================
    ;; create UDP packet
    ;; ========================================================================

    ;; set UDP ports

    ld   hl, #UDP_PORT_TFTP_SERVER * 0x0100    ;; network order
    ld   (outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl

    ;; -----------------------------------------------------------------------
    ;; Set up the TFTP client port to use, as a pseudo-random number in
    ;; range 0x80..0xff. It has a one-valued bit masked in, to overlap with
    ;; UDP_PORT_TFTP_SERVER, and to avoid using port number zero.
    ;; -----------------------------------------------------------------------

    ld   a, r
    or   a, #0x80
    ld   h, a
    ld   (outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ;; ------------------------------------------------------------------------
    ;; set HL := filename pointer,
    ;; E := 0 (zero byte from port number above)
    ;; ------------------------------------------------------------------------

    ex   de, hl

    push hl        ;; remember filename pointer for later

    ;; ------------------------------------------------------------------------
    ;; reset tftp_last_block_no_received
    ;; ------------------------------------------------------------------------

    xor  a, a
    ld   (tftp_last_block_no_received), a

    ;; ------------------------------------------------------------------------
    ;; calculate length of filename; assume length < 240
    ;; (256 - UDP_HEADER_SIZE - TFTP_SIZE_OF_RRQ_PREFIX - TFTP_SIZE_OF_RRQ_OPTION)
    ;;
    ;; assuming HL == filename and A == 0
    ;; ------------------------------------------------------------------------

    ld   c, a      ;; set C := 0
    cpir           ;; find NUL in string pointed to by HL; BC := negated length
    ld   b, a      ;; set B := 0

    sub  a, c
    ld   c, a      ;; negate C

    push bc        ;; remember filename length for later

    ;; set DE to UDP length, in network order (least significant byte in D)
    ;; (E == 0 from above)

    add  a, #UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    ld   d, a

    ld   hl, #eth_broadcast_address    ;; all we know at this point

    ;; -----------------------------------------------------------------------
    ;; B == 0 from string length calculation above
    ;; -----------------------------------------------------------------------

    call udp_create

    ;; append 16-bit TFTP opcode

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

    call udp_send

    .endm