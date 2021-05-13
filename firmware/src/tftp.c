/*
 * Module tftp:
 *
 * Trivial File Transfer Protocol (TFTP, RFC 1350)
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

#include "tftp.h"

#include "eth.h"
#include "globals.h"
#include "ui.h"

/* ------------------------------------------------------------------------- */

/*
 * TFTP opcodes
 */
#define TFTP_OPCODE_RRQ              (1)
#define TFTP_OPCODE_DATA             (3)
#define TFTP_OPCODE_ACK              (4)
#define TFTP_OPCODE_ERROR            (5)

/*
 * Sizes and offsets of individual fields
 */
#define TFTP_SIZE_OF_OPCODE          (2)
#define TFTP_OFFSET_OF_OPCODE        (0)

#define TFTP_OFFSET_OF_BLOCKNO       (2)

#define TFTP_SIZE_OF_RRQ_PREFIX      (13)

#define TFTP_SIZE_OF_RRQ_OPTION      (6)

/*
 * Packet sizes
 */
#define TFTP_SIZE_OF_ACK_PACKET      (4)
#define TFTP_SIZE_OF_ERROR_PACKET    (5)

/* ------------------------------------------------------------------------- */

/* Next TFTP block we expect to receive */
static uint16_t expected_tftp_block_no;

/* Source port currently used by server */
static uint16_t server_port;

/* -------------------------------------------------------------------------
 * Create UDP reply to the sender of the received packet currently processed.
 * Source/destination ports are swapped. Frame class is ETH_FRAME_PRIORITY.
 *
 * Call with BC=number of bytes in payload
 * ------------------------------------------------------------------------- */
void
tftp_create_reply(void)
__naked
{
  __asm

    push bc

    ld   bc, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    push bc

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)
    ld   (_header_template  + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl
    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)
    ld   (_header_template  + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ld   hl, #_rx_eth_adm + ETH_ADM_OFFSETOF_SRC_ADDR
    push hl

    call _udp_create_impl

    pop  af
    pop  af
    pop  af

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
tftp_receive(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; check destination port (should be _tftp_client_port)
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT
    ld   de, #_tftp_client_port
    ld   b, #2
    call _memory_compare
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

tftp_receive_bad_reply::
    ld   a, #FATAL_FILE_NOT_FOUND
    jp   nz, _fail

tftp_receive_got_data::

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

tftp_receive_check_blk_nbr::

    ;; ------------------------------------------------------------------------
    ;; received == expected ?
    ;; ------------------------------------------------------------------------

    xor   a
    sbc   hl, de
    jr    z, tftp_receive_blk_nbr_ok

    ;; ------------------------------------------------------------------------
    ;; received == expected-1 ?   means HL-DE == expected-received == 1
    ;; ------------------------------------------------------------------------

    dec   hl
    ld    a, h
    or    a, l
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_ok::

    ;; ------------------------------------------------------------------------
    ;; check server port number:
    ;; must be the same as used for first packet
    ;; ------------------------------------------------------------------------

    ld    hl, (_server_port)
    ;; BC was loaded above, early on; holds server-side port nbr from packet
    xor   a
    sbc   hl, bc
    jr    nz, tftp_receive_error

tftp_receive_blk_nbr_and_port_ok::

    ;; ========================================================================
    ;; reply with ACK packet
    ;; ========================================================================

    ld    bc, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET
    call    _tftp_create_reply

    ld    de, #TFTP_SIZE_OF_OPCODE
    ld    hl, #tftp_receive_ack_opcode
    call    _enc28j60_write_memory_cont

    ld    e, #TFTP_SIZE_OF_OPCODE    ;; D==0 here
    ld    hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_BLOCKNO
    call    _enc28j60_write_memory_cont

    call  _ip_send

    ;; ========================================================================
    ;; if expected block nbr == received, increase expected and accept the data
    ;; ========================================================================

    ld    hl, (_expected_tftp_block_no)
    ld    d, h
    ld    e, l

    push  ix
    pop   bc
    xor   a
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
    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH
    ld  d, (hl)
    inc hl
    ld  e, (hl)    ;; network order
    ex  de, hl     ;; HL is now UDP length, including UDP + TFTP headers
    ld  de, #UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    xor a          ;; clear C flag
    sbc hl, de
    ld  b, h
    ld  c, l       ;; BC is now payload length, 0..512

    ;; ------------------------------------------------------------------------
    ;; check if BC == 0x200; store result of comparison in alternate AF
    ;; (BC should be exactly 0x200 for all DATA packets except the last one,
    ;; so we are done if B != 2)
    ;; ------------------------------------------------------------------------

    ld  a, #2
    cp  a, b
00001$:
    ex  af, af'             ;; '

    ld  de, (_tftp_write_pos)
    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ldir
    ld  (_tftp_write_pos), de

    ;; ------------------------------------------------------------------------
    ;; If a full TFTP packet was loaded, return.
    ;; ------------------------------------------------------------------------

    ex  af, af'             ;; '
    ret z

    ;; ------------------------------------------------------------------------
    ;; This was the last packet: execute the loaded binary.
    ;; ------------------------------------------------------------------------

    jp  _stage2

tftp_receive_error::

    ld    bc, #UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET
    call  _tftp_create_reply

    ld    de, #TFTP_SIZE_OF_ERROR_PACKET
    ld    hl, #tftp_receive_error_packet
    call  _enc28j60_write_memory_cont

    jp    _ip_send

    ;; ------------------------------------------------------------------------
    ;; ACK is two bytes: 0, 4
    ;; ERROR is five bytes: 0, 5, 0, 4, 0
    ;; ------------------------------------------------------------------------

tftp_receive_error_packet::
    .db   0, TFTP_OPCODE_ERROR        ;; opcode in network order
tftp_receive_ack_opcode::
    .db   0, 4                        ;; illegal TFTP operation, network order
    .db   0                           ;; no particular message

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
tftp_read_request(const char *filename)
__naked
{
  (void) filename;

  __asm

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
    ld   h, a
    ld   (_tftp_client_port), hl
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ;; calculate length of filename

    pop  bc
    pop  hl        ;; HL now points to filename
    push hl
    push bc

    ld   d, h
    ld   e, l

    xor  a
    ld   b, a
    ld   c, a
00001$:
    inc  bc
    cp   a, (hl)
    inc  hl
    jr   nz, 00001$

    push bc        ;; remember filename length for later
    push de        ;; remember filename pointer for later

    ld   hl, #UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    add  hl, bc

    ;; stack arguments and call

    push hl
    ld   hl, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    push hl
    ld   hl, #_eth_broadcast_address    ;; all we know at this point
    push hl
    call _udp_create_impl
    pop  hl
    pop  hl
    pop  hl

    ;; append 16-bit TFTP opcode

    ld   de, #TFTP_SIZE_OF_RRQ_PREFIX
    ld   hl, #tftp_rrq_prefix
    call _enc28j60_write_memory_cont

    ;; filename and length already stacked above

    pop  hl
    pop  de

    call _enc28j60_write_memory_cont

    ;; append option ("octet" mode), happens to be 6 bytes, like a MAC address

    ld   hl, #tftp_rrq_option
    call _enc28j60_write_6b

    jp   _ip_send

    ;; ------------------------------------------------------------------------
    ;; constant data for outgoing TFTP packets
    ;; ------------------------------------------------------------------------

tftp_rrq_option::
    .ascii "octet"             ;; trailing NUL pinched from following packet
tftp_rrq_prefix::
    .db  0, TFTP_OPCODE_RRQ    ;; opcode in network order
    .ascii "speccyboot/"

  __endasm;
}
