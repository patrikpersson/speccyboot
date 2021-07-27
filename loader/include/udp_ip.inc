;; Module udp_ip:
;;
;; User Datagram Protocol (UDP, RFC 768)
;; Internet Protocol (IP, RFC 791)
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

;; ----------------------------------------------------------------------------
;; IP
;; ----------------------------------------------------------------------------

IPV4_ADDRESS_SIZE              = 4

IPV4_HEADER_SIZE               = 20

IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH = 0
IPV4_HEADER_OFFSETOF_TOTAL_LENGTH       = 2
IPV4_HEADER_OFFSETOF_PROT               = 9
IPV4_HEADER_OFFSETOF_CHECKSUM           = 10
IPV4_HEADER_OFFSETOF_SRC_ADDR           = 12
IPV4_HEADER_OFFSETOF_DST_ADDR           = 16

;; ----------------------------------------------------------------------------
;; UDP
;; ----------------------------------------------------------------------------

IP_PROTOCOL_UDP                = 17

UDP_HEADER_SIZE                = 8

UDP_PORT_BOOTP_SERVER          = 67
UDP_PORT_BOOTP_CLIENT          = 68
UDP_PORT_TFTP_SERVER           = 69

UDP_HEADER_OFFSETOF_SRC_PORT   = 0
UDP_HEADER_OFFSETOF_DST_PORT   = 2
UDP_HEADER_OFFSETOF_LENGTH     = 4
UDP_HEADER_OFFSETOF_CHECKSUM   = 6

;; ----------------------------------------------------------------------------
;; Cumulative IP checksum
;; ----------------------------------------------------------------------------

    .globl _ip_checksum

;; ----------------------------------------------------------------------------
;; _ip_config   (defined as constant in globals.inc)
;;
;; IP configuration (host + server addresses, 4 + 4 bytes)
;; ----------------------------------------------------------------------------

IP_CONFIG_HOST_ADDRESS_OFFSET  = 0
IP_CONFIG_TFTP_ADDRESS_OFFSET  = 4

;; ----------------------------------------------------------------------------
;; Header data for BOOTP; defined in init.asm
;; ----------------------------------------------------------------------------

    .globl bootrequest_header_data
    .globl bootrequest_xid

;; ============================================================================

;; ----------------------------------------------------------------------------
;; Create UDP packet (IP + UDP headers). Defined in stack.asm.
;;
;; B: must be 0
;; HL: points to destination MAC address
;; DE: UDP length, including UDP_HEADER_SIZE (NETWORK ORDER)
;; ----------------------------------------------------------------------------

    .globl udp_create
