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

    .module bootp
    .optsdcc -mz80

    .include "include/bootp.inc"

    .include "include/enc28j60.inc"
    .include "include/eth.inc"
    .include "include/globals.inc"
    .include "include/tftp.inc"
    .include "include/udp_ip.inc"
    .include "include/ui.inc"
    .include "include/util.inc"

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

    .area _CODE

;; ############################################################################
;; _bootp_init
;; ############################################################################

_bootp_init:

    ;; ========================================================================
    ;; Presentation
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; print 'SpeccyBoot x.y  BOOTP TFTP' at (0,0)
    ;; ------------------------------------------------------------------------

    ld    hl, #title_str       ;; 'SpeccyBoot x.y'
    ld    de, #0x4000          ;; coordinates (0,0)

bootp_print_str::
    ld   a, (hl)
    inc  hl
    or   a, a
    jr   z, bootp_print_done
    ld   c, d

    exx

    ;; use of alternate registers:
    ;; HL=font data, BC=temp

    ld   l, a
    ld   h, #0
    add  hl, hl
    add  hl, hl
    add  hl, hl
    ld   bc, #_font_data - 32 * 8
    add  hl, bc

    ld   b, #8
bootp_print_loop::
    ld   a, (hl)
    exx
    ld   (de), a
    inc  d
    exx
    inc  hl
    djnz bootp_print_loop

    exx
    ld   d, c
    inc  e
    jr   bootp_print_str
bootp_print_done::

    ;; ------------------------------------------------------------------------
    ;; attributes for 'SpeccyBoot' heading: white ink, black paper, bright
    ;; ------------------------------------------------------------------------

    ld    hl, #ATTRS_BASE      ;; (0,0)
    ld    b, #14
bootp_attr_lp1::
    ld    (hl), #(WHITE | (BLACK << 3) | BRIGHT)
    inc   hl
    djnz  bootp_attr_lp1

    ;; ------------------------------------------------------------------------
    ;; attributes for 'BOOTP' indicator: white ink, black paper, flash, bright
    ;; ------------------------------------------------------------------------

    ld    hl, #ATTRS_BASE + 16     ;; (0,16)
    ld    b, #5
bootp_attr_lp2::
    ld    (hl), #(WHITE | (BLACK << 3) | BRIGHT | FLASH)
    inc   hl
    djnz  bootp_attr_lp2

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

    ld   hl, #UDP_PORT_BOOTP_CLIENT * 0x100    ;; network order
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl
    ld   h, #UDP_PORT_BOOTP_SERVER
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl
    ld   hl, #UDP_HEADER_SIZE + BOOTP_PACKET_SIZE
    push hl
    ld   hl, #_eth_broadcast_address   ;; works for IP broadcast too
    push hl
    push hl
    call _udp_create_impl
    pop  af
    pop  af
    pop  af

    ;; ------------------------------------------------------------------------
    ;; part 1: 8 bytes of header (constants)
    ;; ------------------------------------------------------------------------

    ld   de, #BOOTP_PART1_SIZE
    ld   hl, #bootrequest_header_data
    call _enc28j60_write_memory_cont

    ;; ------------------------------------------------------------------------
    ;; part 2: 20 bytes of zeros
    ;; use VRAM as source of 20 zero-valued bytes
    ;; ------------------------------------------------------------------------

    ld   e, #BOOTP_PART2_SIZE       ;; D==0 here
    ld   hl, #0x4800
    push hl
    call _enc28j60_write_memory_cont

    ;; ------------------------------------------------------------------------
    ;; part 3: 6 bytes of MAC address
    ;; ------------------------------------------------------------------------

    ld   hl, #_eth_local_address
    call _enc28j60_write_6b

    ;; ------------------------------------------------------------------------
    ;; part 4: 266 bytes of zeros
    ;; use VRAM as source of 266 zero-valued bytes
    ;; ------------------------------------------------------------------------

    ld   de, #BOOTP_PART4_SIZE
    pop  hl                       ;; HL is now 0x4800, zeros in VRAM
    call _enc28j60_write_memory_cont

    jp   _ip_send

    ;; ========================================================================
    ;; data for the first 8 bytes of the BOOTREQUEST
    ;; ========================================================================

bootrequest_header_data::
    .db   BOOTREQUEST        ;; op
    .db   1                  ;; htype (10mbps Ethernet)
    .db   6                  ;; hlen
    .db   0                  ;; hops

bootrequest_xid::
    ;; use first four bytes of title_str ("Spec") for XID

/*
 * Stringification using C preprocessor, see e.g.,
 * https://gcc.gnu.org/onlinedocs/gcc-4.8.5/cpp/Stringification.html
 */

#define str(s) str2(s)
#define str2(s) #s

title_str::
    .ascii "SpeccyBoot "
    .ascii str(VERSION)
    .ascii "  BOOTP TFTP"
    .db   0

;; ############################################################################
;; _bootp_receive
;; ############################################################################

_bootp_receive:

    ;; ------------------------------------------------------------------------
    ;; only accept BOOTREPLY packets with correct XID
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_OP)
    cp   a, #BOOTREPLY
    ret  nz
    ld   hl, #bootrequest_xid
    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_XID
    ld   b, #4
    call _memory_compare
    ret  nz

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

    ld    hl, #(ATTRS_BASE + 16)
    ld    bc, #0x0607    ;; 6 chars, white ink
tftp_attr_lp1::
    ld    (hl), c
    inc   hl
    djnz  tftp_attr_lp1

    ld    b, #4      ;; 4 chars
tftp_attr_lp2::
    ld    (hl), #(WHITE | (BLACK << 3) | BRIGHT | FLASH)
    inc   hl
    djnz  tftp_attr_lp2

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
#ifdef STAGE2_IN_RAM
    .ascii 'spboot.bin'
#else
    .ascii 'snapshots.lst'
#endif
    .db   0
