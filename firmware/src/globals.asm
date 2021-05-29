;; Module globals:
;;
;; Shared state (buffer for received frame, system mode)
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

    .module globals
    .optsdcc -mz80

    .include "include/globals.inc"

    .include "include/context_switch.inc"
    .include "include/eth.inc"
    .include "include/udp_ip.inc"
    .include "include/tftp.inc"

    .area _DATA

;; ----------------------------------------------------------------------------
;; buffer for received packet data
;; ----------------------------------------------------------------------------

_rx_frame:
    .ds   RX_FRAME_SIZE

;; ============================================================================
;; IP/UDP/TFTP stuff
;; ============================================================================

_ip_config:
   .ds    4 * 2     ;; IP v4 addressed for this host + TFTP server

_header_template:   ;; header template for outgoing UDP packets
   .ds   IPV4_HEADER_SIZE + UDP_HEADER_SIZE

_ip_checksum:
   .ds   2

_tftp_client_port:
   .ds   2       ;; client-side UDP port for TFTP

_tftp_write_pos:
   .ds   2       ;; initialized in init.asm

;; ----------------------------------------------------------------------------
;; If non-NULL, this function is called for every received TFTP packet
;; (instead of regular raw data file handling)
;; ----------------------------------------------------------------------------

_tftp_receive_hook:
   .ds   2
