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

VERSION_STAGE1 = 3
VERSION_LOADER = 0

;; ----------------------------------------------------------------------------
;; The VERSION_MAGIC byte is encoded as a LD b, b' instruction,
;; so it is harmless to execute as part of the jump to the stage 2 loader
;; ----------------------------------------------------------------------------

VERSION_MAGIC  = VERSION_STAGE1 | 0x40

;; ============================================================================
;; to facilitate single-page addressing, page 0x5B is laid out as follows:
;;
;; 5b00..5b1f       stack, 0x20 bytes
;; 5b20..5b27       IP config (2x IPv4 addresses, 8 bytes)
;; 5b28..5b43       IP/UDP header template, 20+8 bytes
;; 5b44..5b7a       stored .z80 snapshot header, 55 bytes
;; 5b7b..5b8e       IP header (20 bytes)
;; 5b8f..5b96       UDP header (8 bytes)
;; 5b97..5b9a       TFTP DATA header (4 bytes)
;; 5b9b..5bd1       .z80 snapshot header, 55 bytes
;;
;; The BOOTP header is available here in part:
;;
;; 5b97..5bff       first 69 bytes of BOOTP packet, including
;;                  OP, XID, YIADDR, first byte of SNAME
;; ============================================================================

STACK_SIZE             = 0x20
_stack_top             = 0x5b00 + STACK_SIZE

_ip_config             = 0x5b20             ;; size 8
_header_template       = 0x5b28             ;; size 0x1c  (20+8)
stored_snapshot_header = 0x5b44             ;; size 0x37  (Z80_HEADER_RESIDENT_SIZE)
_rx_frame              = 0x5b7b             ;; size 0x220 (RX_FRAME_SIZE)
digit_font_data        = 0x5d9b             ;; size 0x4e  (10*8 - 2)

;; _DATA segment can start at 0x5d9b + 0x4e = 0x5de9

;; ============================================================================
;; use the two-byte slots between digits to keep resident RAM to a minimum
;; ============================================================================

;; ----------------------------------------------------------------------------
;; IP checksum
;; ----------------------------------------------------------------------------

_ip_checksum            = digit_font_data + 0 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; eth.asm: points to the frame currently being created
;; ----------------------------------------------------------------------------

_current_txbuf          = digit_font_data + 2 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; context_switch.asm: 16-bit word for memory configuration
;;  low byte:  flag (zero means 48k, non-zero means 128k)
;;  high byte: 128k memory configuration (0x7ffd)
;; ----------------------------------------------------------------------------

memory_state            = digit_font_data + 3 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; tftp.asm: next TFTP block we expect to receive
;; ----------------------------------------------------------------------------

_expected_tftp_block_no = digit_font_data + 4 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; tftp.asm: source port currently used by server
;; ----------------------------------------------------------------------------

_server_port            = digit_font_data + 5 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; tftp.asm: if non-NULL, this function is called for every received TFTP
;; packet (instead of regular raw data file handling)
;; ----------------------------------------------------------------------------

_tftp_receive_hook      = digit_font_data + 6 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; util.asm: ick count, increased by 2 (!) by the 50Hz timer ISR in init.asm.
;; Means that the high byte is increased every 2.56 seconds.
;; ----------------------------------------------------------------------------

_timer_tick_count       = digit_font_data + 7 * 8 + 6   ;; 2 bytes

;; ----------------------------------------------------------------------------
;; z80_loader.asm: expected and currently loaded no. of kilobytes,
;; for progress display
;; ----------------------------------------------------------------------------

kilobytes_loaded        = digit_font_data + 8 * 8 + 6   ;; 1 byte

;; ----------------------------------------------------------------------------
;; tftp.asm: high byte of chosen UDP client port
;; (low byte is always 0x45, network order)
;; ----------------------------------------------------------------------------

_tftp_client_port       = digit_font_data + 8 * 8 + 7   ;; 1 byte

;; ============================================================================

;; ----------------------------------------------------------------------------
;; RAM location for font data, copied from BASIC ROM
;; ----------------------------------------------------------------------------

_font_data  = 0xfd00                  ;; full font, but overwritten by snapshot

;; ----------------------------------------------------------------------------
;; Number of snapshot names displayed at a time (in the menu)
;; ----------------------------------------------------------------------------

DISPLAY_LINES = 20

;; ----------------------------------------------------------------------------
;; Address of stage 2 loader
;; ----------------------------------------------------------------------------

    .globl stage2_start

;; ----------------------------------------------------------------------------
;; Number of snapshots (one byte, directly after stage 2 loader)
;; ----------------------------------------------------------------------------

    .globl nbr_snapshots

;; ----------------------------------------------------------------------------
;; Array of pointers to snapshot file names. The array size is nbr_snapshots.
;; (one word, directly after nbr_snapshots)
;; ----------------------------------------------------------------------------

    .globl snapshot_array


;; ----------------------------------------------------------------------------
;; A JP (IX) instruction, used as a CALL target from z80_loader
;; ----------------------------------------------------------------------------

    .globl jp_ix_instr
