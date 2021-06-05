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
;; Position on screen for TFTP error display
;; ----------------------------------------------------------------------------

TFTP_VRAM_ERROR_POS    = 0x4800                 ;; (8, 0)

;; ----------------------------------------------------------------------------
;; UDP client-side port for UDP transfers (checked in udp_ip.asm)
;; ----------------------------------------------------------------------------

    .globl _tftp_client_port

;; ----------------------------------------------------------------------------
;; Position to write loaded TFTP data to (initialized in init.asm)
;; ----------------------------------------------------------------------------

    .globl _tftp_write_pos

;; ----------------------------------------------------------------------------
;; RAM pointer to routine to call on TFTP data.
;; Initially zero (and unused), then set by stage 2 to a hook in z80_loader
;; to enable snapshot loading.
;;
;; When this routine is called, registers are set as follows:
;;
;; DE: number of bytes loaded, excluding IP/UDP/TFTP headers
;; ----------------------------------------------------------------------------

    .globl _tftp_receive_hook

;; ----------------------------------------------------------------------------
;; NUL-terminated default filename for stage 2 loader ('stage2')
;; ----------------------------------------------------------------------------

    .globl tftp_default_file

;; ----------------------------------------------------------------------------
;; Called by UDP when a TFTP packet has been identified
;; ----------------------------------------------------------------------------

    .globl tftp_receive

;; ----------------------------------------------------------------------------
;; Initiate a file transfer from server.
;;
;; HL: pointer to NUL-terminated file name
;; ----------------------------------------------------------------------------
    .globl tftp_read_request
