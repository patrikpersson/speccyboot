/*
* Module bootp:
*
* Boot Protocol (BOOTP, RFC 951)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2021-  Patrik Persson
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

#include "bootp.h"

#include "eth.h"
#include "globals.h"
#include "udp_ip.h"
#include "syslog.h"
#include "tftp.h"
#include "ui.h"

/* ========================================================================= */

/* BOOTP operations */
#define BOOTREQUEST         (1)
#define BOOTREPLY           (2)

#define BOOTP_PART1_SIZE    (8)
#define BOOTP_PART2_SIZE    (20)
#define BOOTP_PART3_SIZE    (6)
#define BOOTP_PART4_SIZE    (266)

#define BOOTP_PACKET_SIZE                                                    \
  (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + BOOTP_PART4_SIZE)

#define BOOTP_OFFSETOF_OP       (0)
#define BOOTP_OFFSETOF_XID      (4)
#define BOOTP_OFFSETOF_YIADDR   (BOOTP_PART1_SIZE + 8)

#define BOOTP_OFFSETOF_SNAME                                                 \
  (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + 10)

#define BOOTP_OFFSETOF_FILE                                                  \
  (BOOTP_PART1_SIZE + BOOTP_PART2_SIZE + BOOTP_PART3_SIZE + 10 + 64)

/* ------------------------------------------------------------------------- */

void
bootp_init(void)
__naked
{
  __asm

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

    ld   bc, #UDP_PORT_BOOTP_CLIENT * 0x100    ;; network order
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), bc
    ld   b, #UDP_PORT_BOOTP_SERVER
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), bc
    ld   hl, #ETH_FRAME_PRIORITY
    push hl
    ld   hl, #UDP_HEADER_SIZE + BOOTP_PACKET_SIZE
    push hl
    ld   hl, #_eth_broadcast_address   ;; works for IP broadcast too
    push hl
    push hl
    call _udp_create_impl
    pop  af
    pop  af
    pop  af
    pop  af

    ;; ------------------------------------------------------------------------
    ;; part 1: 8 bytes of header (constants)
    ;; ------------------------------------------------------------------------

    ld   bc, #BOOTP_PART1_SIZE
    push bc
    ld   hl, #bootrequest_header_data
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; ------------------------------------------------------------------------
    ;; part 2: 20 bytes of zeros
    ;; use address 0x4814 as source of 20 zero-valued bytes (VRAM)
    ;; ------------------------------------------------------------------------

    ld   c, #BOOTP_PART2_SIZE
    push bc
    ld   b, #0x48
    push bc
    call _enc28j60_write_memory_cont
    pop  bc
    pop  bc

    ;; ------------------------------------------------------------------------
    ;; part 3: 6 bytes of MAC address
    ;; ------------------------------------------------------------------------

    ld   c, #BOOTP_PART3_SIZE    ;; just popped BC with B==0
    push bc
    ld   hl, #_eth_local_address
    push hl
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    ;; ------------------------------------------------------------------------
    ;; part 4: 266 bytes of zeros
    ;; use address 0x480a as source of 266 zero-valued bytes (VRAM)
    ;; ------------------------------------------------------------------------

    ld   bc, #BOOTP_PART4_SIZE
    push bc
    ld   b, #0x48    ;; C is 0x0A from assignment above
    push bc
    call _enc28j60_write_memory_cont
    pop  hl
    pop  bc

    jp   _udp_send

    ;; ========================================================================
    ;; data for the first 8 bytes of the BOOTREQUEST
    ;; ========================================================================

bootrequest_header_data::
    .db   BOOTREQUEST        ;; op
    .db   1                  ;; htype (10mbps Ethernet)
    .db   6                  ;; hlen
    .db   0                  ;; hops
bootrequest_xid::
    .db   0x5a, 0x58, 0x38, 0x32  ;; 'ZX82'

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
bootp_receive(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; only accept BOOTREPLY packets with correct XID
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_OP)
    cp   a, #BOOTREPLY
    ret  nz
    ld   hl, #bootrequest_xid
    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_XID
    ld   b, #4
bootp_receive_check_xid_loop::
    ld   a, (de)
    cp   a, (hl)
    ret  nz
    inc  hl
    inc  de
    djnz bootp_receive_check_xid_loop

    ;; ------------------------------------------------------------------------
    ;; Copy two IP addresses (8 bytes, local + server address) from packet to
    ;; local IP configuration. This means that the TFTP server address will
    ;; default to the DHCP server address.
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_YIADDR
    ld   de, #_ip_config
    ld   bc, #8
    ldir

    ;; ------------------------------------------------------------------------
    ;; Check SNAME field for a dotted-decimal IP address (four octets)
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_SNAME
    ld   a, (hl)
    or   a, a
    jr   z, bootp_receive_sname_done

    ld   de, #_ip_config + IP_CONFIG_TFTP_ADDRESS_OFFSET
    ld   b, #4  ;; four octets

bootp_receive_octet_loop:
    push bc
    push de
    call bootp_receive_parse_octet
    ld   a, c
    pop  de
    pop  bc

    ld   (de), a
    inc  de

    ld   a, (hl)
    inc  hl
    or   a      ;; can only be '.' or NUL here
    jr   nz, bootp_receive_more_octets

    ;;   HL apparently points to NUL. This is only OK after last octet (B==1)

    ld   a, b
    dec  a
    jr   nz , #bootp_receive_invalid_address

bootp_receive_more_octets:
    djnz bootp_receive_octet_loop

    ;; If we got here, the last octet was apparently followed by a period.
    ;; This is technically wrong, but accepted.

bootp_receive_sname_done::

    ;; ------------------------------------------------------------------------
    ;; Send TFTP read request for filename in FILE field, or, if none given,
    ;; use the default 'spboot.bin'
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_FILE
    or   a, (hl)      ;; we know A is zero here
    jr   nz, 00001$
    ld   hl, #bootp_receive_default_file
00001$:
    push hl
    call _tftp_read_request
    pop  hl

    ret

bootp_receive_parse_octet::

    ;; ========================================================================
    ;; Subroutine:
    ;;
    ;; Parse decimal number (0..255) at HL, return value in C.
    ;; Destroys A, BC, DE, and F.
    ;;
    ;; Afterwards HL points to the first byte after the number
    ;; (either '.' or NUL).
    ;; Fails with FATAL_INVALID_BOOT_SERVER directly if a character other than
    ;; 0123456789. or NUL is found.
    ;; ========================================================================

    ld   bc, #0x0300        ;; B := 3, C := 0
    ld   a, (hl)
00001$:
    inc  hl
    sub  a, #'0'
    jr   c, bootp_receive_invalid_address
    cp   a, #10
    jr   nc, bootp_receive_invalid_address
    ld   d, a          ;; D now holds digit value 0..9

    ;; A := C*10, destroys E

    ld   a, c
    add  a, a
    add  a, a
    add  a, a
    ld   e, a  ;; now E = C*8
    ld   a, c
    add  a, a  ;; C*2
    add  a, e

    add  a, d
    ld   c, a  ;; C := C*10 + D

    ld   a, (hl)
    or   a, a
    ret  z
    cp   a, #'.'
    ret  z

    djnz 00001$

    ;; If we got here, it means we had three digits followed by something else
    ;; than '.' or NUL. Fall through to error routine below.

bootp_receive_invalid_address::

    ;; ERROR: boot server name is not a dotted-decimal IP address

    ld   a, #FATAL_INVALID_BOOT_SERVER
    jp   _fail

bootp_receive_default_file::
    .ascii 'spboot.bin'
    .db    0

  __endasm;
}