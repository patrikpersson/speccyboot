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

_tftp_write_pos:
   .ds   2

_chunk_bytes_remaining:
   .ds   2

;; ----------------------------------------------------------------------------
;; next TFTP block we expect to receive
;; ----------------------------------------------------------------------------

_expected_tftp_block_no:
    .ds   2

;; ----------------------------------------------------------------------------
;; source port currently used by server
;; ----------------------------------------------------------------------------

_server_port:
    .ds   2

;; ----------------------------------------------------------------------------
;; function called for every received TFTP packet
;; ----------------------------------------------------------------------------

tftp_state:
    .ds    2

;; ----------------------------------------------------------------------------
;; high byte of chosen UDP client port
;; (low byte is always 0x45, network order)
;; ----------------------------------------------------------------------------

_tftp_client_port:
    .ds    1
