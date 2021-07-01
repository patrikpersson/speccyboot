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
;; packet counter for TFTP
;; ----------------------------------------------------------------------------

    .globl _expected_tftp_block_no

;; ----------------------------------------------------------------------------
;; UDP port used by TFTP server
;; ----------------------------------------------------------------------------

    .globl _server_port

;; ----------------------------------------------------------------------------
;; UDP client-side port for UDP transfers (checked in udp_ip.asm)
;; Only the low byte (Z80 order) / high byte (network order) is stored here;
;; the other byte is always UDP_PORT_TFTP_SERVER.
;; ----------------------------------------------------------------------------

    .globl _tftp_client_port

;; ----------------------------------------------------------------------------
;; Position to write loaded TFTP data to (initialized in init.asm)
;; ----------------------------------------------------------------------------

    .globl _tftp_write_pos

;; ----------------------------------------------------------------------------
;; Number of bytes remaining in current .z80 chunk (used by .z80 loader)
;; ----------------------------------------------------------------------------

    .globl _chunk_bytes_remaining

;; ----------------------------------------------------------------------------
;; RAM pointer to routine to call on TFTP data. Initially points to
;; tftp_state_menu_loader, but modified by the .z80 loader to handle snapshots.
;;
;; When this routine is called, registers are set as follows:
;;
;; DE: number of bytes loaded, excluding IP/UDP/TFTP headers
;; ----------------------------------------------------------------------------

    .globl tftp_state

;; ----------------------------------------------------------------------------
;; NUL-terminated default filename for stage 2 loader ('stage2')
;; ----------------------------------------------------------------------------

    .globl tftp_default_file

;; ----------------------------------------------------------------------------
;; Initiate a file transfer from server.
;; Defined in eth.c, using macro prepare_tftp_read_request below.
;;
;; DE: pointer to NUL-terminated file name
;; HL: pointer to TFTP state handler
;; ----------------------------------------------------------------------------

    .globl tftp_read_request

;; ----------------------------------------------------------------------------
;; TFTP initial state: load menu.bin into RAM
;; ----------------------------------------------------------------------------

    .globl tftp_state_menu_loader

;; ============================================================================
;; Macro: executed by UDP when a TFTP packet has been identified.
;; Returns (RET or RET cc) when done.
;; ============================================================================

    .macro  handle_tftp_packet

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
    ;; keep server-side port number in BC
    ;; ------------------------------------------------------------------------

    ld    bc, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)

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
    ;; C flag is clear from either SBC HL, DE (resulting in zero) or OR A, L
    sbc   hl, bc
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_and_port_ok:

    ;; ========================================================================
    ;; reply with ACK packet
    ;; ========================================================================

    ld    de, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET
    call  udp_reply

    rst   enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline
    ;; -----------------------------------------------------------------------

    .db  ack_packet_end - ack_packet_start

ack_packet_start:
    .db   0, TFTP_OPCODE_ACK
ack_packet_end:

    ;; -----------------------------------------------------------------------

    ld    e, #TFTP_SIZE_OF_OPCODE
    ld    hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO
    rst   enc28j60_write_memory_small

    call  ip_send

    ;; ========================================================================
    ;; if expected block nbr == received, increase expected and accept the data
    ;; ========================================================================

    ld    hl, (_expected_tftp_block_no)

    ld    de, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO)
    ld    a, e
    cp    a, h    ;; network order  
    ret   nz
    ld    a, d
    cp    a, l
    ret   nz

    inc   hl
    ld    (_expected_tftp_block_no), hl

    ;; -----------------------------------------------------------------------
    ;; Compute TFTP data length by subtracting UDP+TFTP header sizes
    ;; from the UDP length. Start with the low-order byte in network order;
    ;; hence the "+1" below. Set BC to TFTP payload length, 0..512.
    ;; -----------------------------------------------------------------------

    ld  de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH + 1
    ld  a, (de)                                      ;; UDP length, low byte
    sub a, #(UDP_HEADER_SIZE + TFTP_HEADER_SIZE)  
    ld  c, a                                         ;; TFTP length, low byte
    dec de
    ld  a, (de)                                      ;; UDP length, high byte
    sbc a, #0
    ld  b, a                                         ;; TFTP length, high byte

    ;; ------------------------------------------------------------------------
    ;; Set up registers for TFTP state handlers:
    ;;
    ;; BC: number of bytes left to read from TFTP packet (set above)
    ;; DE: write pointer (destination somewhere in RAM)
    ;; HL: number of bytes remaining in current chunk
    ;; IX: current state
    ;; IY: read pointer (TFTP packet data)
    ;; I:  number of ED ED repetitions remaining
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

    ld    de, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET
    call  udp_reply

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

    jp    ip_send

    .endm

;; ============================================================================
;; Macro: prepare (but do not send) a TFTP read request packet.
;; ============================================================================

    .macro prepare_tftp_read_request

    push de

    ;; ------------------------------------------------------------------------
    ;; Set initial state for TFTP data handling
    ;; ------------------------------------------------------------------------

    ld    (tftp_state), hl

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

    ;; ------------------------------------------------------------------------
    ;; reset _expected_tftp_block_no to 1
    ;; ------------------------------------------------------------------------

    ld   hl, #1
    ld   (_expected_tftp_block_no), hl

    ;; H==0 here

    ld   l, #UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    add  hl, de
    ex   de, hl    ;; DE = UDP length

    ;; HL now equals the filename length. Assume this is less than 256
    ;; (no path component is included). This means that H==0, which is also
    ;; the address page where eth_broadcast_address resides.

    ld   bc, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   l, #<eth_broadcast_address    ;; all we know at this point; H==0 here
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