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

    .include "bootp.inc"

    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ----------------------------------------------------------------------------
;; Location of local and server IP addresses (row 23, columns 0 and 16)
;; ----------------------------------------------------------------------------

LOCAL_IP_POS  = (BITMAP_BASE + 0x1000 + 7*32 + 0)
SERVER_IP_POS = (BITMAP_BASE + 0x1000 + 7*32 + 16)

    .area _CODE

;; ############################################################################
;; bootp_receive
;; ############################################################################

bootp_receive:

    ;; ------------------------------------------------------------------------
    ;; only accept BOOTREPLY packets with correct XID
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_OP)
    cp   a, #BOOTREPLY
    ret  nz
    ld   hl, #bootrequest_xid
    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_XID
    ld   b, #4
    rst  memory_compare
    ret  nz

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

    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_SNAME
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
    jp   nc, fail

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
    jr   nz , #parse_invalid_address

bootp_receive_more_octets:
    djnz bootp_receive_octet_loop

    ;; If we got here, the last octet was apparently followed by a period.
    ;; This is technically wrong, but accepted.

bootp_receive_sname_done:

    ;; ------------------------------------------------------------------------
    ;; Send TFTP read request for filename in FILE field,
    ;; or, if none given, use the default
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_FILE
    xor  a, a
    or   a, (hl)
    jr   nz, 00001$
    ld   hl, #tftp_default_file
00001$:
    call tftp_read_request

    ;; ------------------------------------------------------------------------
    ;; attributes for 'L' indicator: black ink, white paper, bright
    ;; ------------------------------------------------------------------------

    ld    hl, #ATTRS_BASE + 23 * 32                ;; (23, 0)
    ld    (hl), #(BLACK | (WHITE << 3) | BRIGHT)

    ;; ------------------------------------------------------------------------
    ;; attributes for 'S' indicator: black ink, green paper, bright, flash
    ;; ------------------------------------------------------------------------

    ld    l, #<ATTRS_BASE + 23 * 32 + 16           ;; (23, 16)
    ld    (hl), #(BLACK | (GREEN << 3) | BRIGHT | FLASH)

    ;; ------------------------------------------------------------------------
    ;; print 'L', local IP address, 'S', server IP address
    ;; ------------------------------------------------------------------------

    ld    a, #'L'
    ld    de, #LOCAL_IP_POS
    ld    hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    call print_ip_addr

    ld    a, #'S'
    ld    e, #<SERVER_IP_POS

    ;; FALL THROUGH to print_ip_addr


;; ############################################################################
;; Prints IP address, four octets of 1-3 digits, with a  descriptive letter
;; ('L' or 'S') and periods between octets.
;; A = initial letter to print ('L' or 'S')
;; DE = VRAM pointer
;; HL = pointer to IP address
;; AF, BC are destroyed. DE and HL are increased.
;; ############################################################################

    .area _CODE

print_ip_addr:

    call  print_char             ;; initial letter

    ;; DE = VRAM pointer
    ;; HL = IP address
    ;; AF, BC = scratch

    ld    b, #4       ;; loop counter, four octets
00001$:
    push  bc

    ld    a, (hl)
    inc   hl

    cp    a, #10
    jr    c, 00002$        ;; < 10? print only single digit

    ld    b, #100
    cp    a, b
    call  nc, print_div    ;; no hundreds? skip entirely, not even a zero

    ld    b, #10
    call  print_div

00002$:   ;; tens done

    call  print_digit

    pop   bc

    ;; print period?
    dec   b
    ret   z

    ld    a, #'.'
    call  print_char
    jr    00001$           ;; next octet

;; ----------------------------------------------------------------------------
;; Divides A by B, and prints as one digit. Returns remainder in A.
;; Destroys AF'.  '
;; ----------------------------------------------------------------------------

print_div:
    call  a_div_b

    ex    af, af'            ;;'
    ld    a, c

    ;; FALL THROUGH to print_digit


;; ############################################################################

print_digit:
    add  a, #'0'

    ;; FALL THROUGH to print_char


;; ############################################################################
;; _print_char
;; ############################################################################

print_char:

    push hl
    push bc

    add  a, #<((_font_data - 32 * 8) >> 3)
    ld   l, a
    ld   h, #>((_font_data - 32 * 8) >> 3)
    add  hl, hl
    add  hl, hl
    add  hl, hl

    ld   b, #8
    ld   c, d
_print_char_loop:
    ld   a, (hl)
    ld   (de), a
    inc  d
    inc  hl
    djnz _print_char_loop
    ld   d, c

    ex   af, af'            ;;'   bring back A after print_div

    inc  e

    pop  bc
    pop  hl

    ret  nz

    ;; E became zero: means we reached the end of one of the 2K VRAM segments,
    ;; skip to the next one

    ld   a, d
    add  a, #8
    ld   d, a

    ;; A is destroyed here, but this only matters to print_ip_addr, which
    ;; never prints to the end of the line

    ret
