;; Module globals:
;;
;; Shared state (buffer for received frame, system mode)
;;
;; Definitions for bit-banged SPI access, common to C and assembly files.
;; NOTE: requires "spi_asm.h" to be included by C preprocessor (for ifdefs).
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2012-  Patrik Persson & Imrich Kolkol
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

;; ============================================================================
;; Version information
;; ============================================================================

VERSION_MAJOR = 2
VERSION_MINOR = 1

VERSION_MAGIC = 0x5B00 | VERSION_MAJOR << 4 | VERSION_MINOR

;; ----------------------------------------------------------------------------
;; Stack address (defined here so init.asm can find it via linker)
;; ----------------------------------------------------------------------------

STACK_SIZE  = 0x20
_stack_top  = 0x5b00 + STACK_SIZE

;; ----------------------------------------------------------------------------
;; Size of receive buffer:
;; IPV4_HEADER_SIZE
;;  + UDP_HEADER_SIZE
;;  + TFTP_HEADER_SIZE
;;  + TFTP_DATA_MAXSIZE
;; ----------------------------------------------------------------------------

RX_FRAME_SIZE = 20 + 8 + 4 + 512

_font_data  = 0xfd00                  ;; full font, but overwritten by snapshot

;; ----------------------------------------------------------------------------
;; Snapshot header, copied here while the snapshot is loading.
;; This is only used while the bank 0x4000..0x7fff is loading.
;; Kept in a single RAM page; simplifies adressing.
;; ----------------------------------------------------------------------------

_snapshot_header = 0x7f00             ;; snapshot header, temporary storage

;; ----------------------------------------------------------------------------

    .globl _rx_frame
    .globl _kilobytes_loaded
    .globl _tftp_client_port
    .globl _tftp_write_pos
    .globl _ip_config
    .globl _header_template
    .globl _ip_checksum
    .globl _tftp_receive_hook
    .globl _snapshot_list
    .globl stage2_start
