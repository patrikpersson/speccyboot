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

VERSION_STAGE1 = 5

;; ===========================================================================
;; ROM1 data, to be copied to RAM
;; ===========================================================================

;; ---------------------------------------------------------------------------
;; Keymap (mapping scancode -> ASCII)
;; ---------------------------------------------------------------------------

ROM_KEYMAP_ADDR   = 0x0205
ROM_KEYMAP_LENGTH = 0x28

;; ---------------------------------------------------------------------------
;; Data block with 6x0xff and keymap in a single chunk
;; ---------------------------------------------------------------------------

ROM_DATA_ADDR     = 0x0060
ROM_DATA_LENGTH   = (ROM_KEYMAP_ADDR + ROM_KEYMAP_LENGTH - ROM_DATA_ADDR)

;; ---------------------------------------------------------------------------
;; Font data
;; ---------------------------------------------------------------------------

ROM_FONTDATA_ADDR = 0x3d00

;; ============================================================================
;; to facilitate single-page addressing, page 0x5B is laid out as follows:
;;
;; 5b00..5b1f       stack, 0x20 bytes
;; 5b20..5b3b       IP/UDP headers for outgoing packets, 20+8 bytes
;;
;; 5b44..5b7a       stored .z80 snapshot header, 55 bytes
;; 5b7f..5b92       received IP header (20 bytes)
;; 5b93..5b9a       received UDP header (8 bytes)
;; 5b9b..5b9e       received TFTP DATA header (4 bytes)
;; 5b9f..5bd5       received .z80 snapshot header, 55 bytes
;;
;; The three bytes kilobytes_loaded, kilobytes_expected, and ram_config are
;; located together, so two of them can be accessed in a single 16-bit word
;; (kilobytes_loaded+expected, or kilobytes_expected+ram_config)
;;
;; The BOOTP header is available here in part:
;;
;; 5b9a..5bff       first 66 bytes of BOOTP packet, including
;;                  OP, XID, YIADDR, first byte of SNAME
;; ============================================================================

;; ---------------------------------------------------------------------------
;; NOTE: init.asm relies on the low byte of _stack_top being 0x20.
;; ---------------------------------------------------------------------------

STACK_SIZE                  = 0x20
_stack_top                  = 0x5b00 + STACK_SIZE

;; ---------------------------------------------------------------------------
;; Outgoing_header is placed so IP addresses have (Z80) addresses
;; divisible by 4 (used in stack.asm, print_ip_addr).
;;
;; The first four bytes are set to a non-zero initial value in init.asm, but
;; overwritten with a template header in stack.asm.
;; ---------------------------------------------------------------------------

outgoing_header             = 0x5b20             ;; size 0x1c  (20+8)

                                                 ;; 8 unused (empty) bytes here

stored_snapshot_header      = 0x5b44             ;; size 0x37  (Z80_HEADER_RESIDENT_SIZE)
kilobytes_loaded            = 0x5b7b             ;; size 1
kilobytes_expected          = 0x5b7c             ;; size 1
ram_config                  = 0x5b7d             ;; size 1
tftp_last_block_no_received = 0x5b7e             ;; size 1
rx_frame                    = 0x5b7f             ;; size 0x220 (RX_FRAME_SIZE)

;; _DATA segment can start at 0x5d9f

;; ============================================================================

;; ----------------------------------------------------------------------------
;; RAM location for font data, copied from BASIC ROM
;; ----------------------------------------------------------------------------

_font_data             = 0x5e00

;; ----------------------------------------------------------------------------
;; RAM copy of 6x 0xff bytes and keymap
;; ----------------------------------------------------------------------------

copied_rom_data        = 0x6100      ;; immediately after _font_data

eth_broadcast_address  = copied_rom_data
keymap                 = copied_rom_data + ROM_KEYMAP_ADDR - ROM_DATA_ADDR

;; ============================================================================

;; ----------------------------------------------------------------------------
;; Number of snapshot names displayed at a time (in the menu)
;; ----------------------------------------------------------------------------

DISPLAY_LINES = 22

;; ----------------------------------------------------------------------------
;; Number of snapshots (from loaded 'menu.dat')
;; ----------------------------------------------------------------------------

    .globl nbr_snapshots

;; ----------------------------------------------------------------------------
;; Array of pointers to snapshot file names. The array size is nbr_snapshots.
;; (from loaded 'menu.dat')
;; ----------------------------------------------------------------------------

    .globl snapshot_array

;; ----------------------------------------------------------------------------
;; A JP (IX) instruction, used as a CALL target, to emulate a CALL (IX)
;; (defined in z80_loader.asm)
;; ----------------------------------------------------------------------------

    .globl jp_ix_instr
