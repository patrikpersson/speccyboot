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

;; ============================================================================

    .area _CODE

tftp_state_menu_loader:

    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ldir
    ld  (_tftp_write_pos), de

    ;; ------------------------------------------------------------------------
    ;; If a full TFTP packet was loaded, return.
    ;; (BC above should be exactly 0x200 for all DATA packets except the last
    ;; one, never larger; so we are done if A != 2 here)
    ;; ------------------------------------------------------------------------

    cp  a, #2
    ret z           ;; BC==0 here, indicating that we are done

    ;; ========================================================================
    ;; This was the last packet of the stage 2 binary:
    ;; check version signature and run the stage 2 loader
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; check version signature
    ;; ------------------------------------------------------------------------

    ld  hl, #stage2_start
    ld  a, (hl)
    cp  a, #VERSION_MAGIC
version_mismatch:
    jr  nz, fail_version_mismatch

    ;; ------------------------------------------------------------------------
    ;; At this point HL points to the VERSION_MAGIC byte. This is encoded as
    ;; a LD r, r' instruction (binary 01xxxxxx) and harmless to execute.
    ;; One INC HL is saved this way.
    ;; ------------------------------------------------------------------------

    jp  (hl)

tftp_default_file:
    .ascii 'menu.bin'
    .db    0


;; ############################################################################
;; fail
;; fail_version_mismatch
;; ############################################################################

    .area _CODE

fail_version_mismatch:
    ld  a, #VERSION_STAGE1
    call show_attr_digit_right
    ld  a, #FATAL_VERSION_MISMATCH

    ;; FALL THROUGH to fail

fail:

    di
    out (ULA_PORT), a
    halt
