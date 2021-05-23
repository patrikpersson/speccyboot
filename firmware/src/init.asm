  ;; init:
  ;;
  ;; System initialization, RST & interrupt handlers
  ;;
  ;; Part of SpeccyBoot <https://patrikpersson.github.io/speccyboot/>
  ;; --------------------------------------------------------------------------
  ;;
  ;; Copyright (c) 2009-  Patrik Persson & Imrich Kolkol
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
  ;; --------------------------------------------------------------------------

  .module init
  .optsdcc -mz80

#include "spi_asm.h"

  .include "include/spi.inc"
  .include "include/enc28j60.inc"
  .include "include/globals.inc"
  .include "include/menu.inc"
  .include "include/udp_ip.inc"
  .include "include/util.inc"

  ;; --------------------------------------------------------------------------

  .area  _HEADER (ABS)

  ;; --------------------------------------------------------------------------

  .org  0

  im    1                    ;; 2 bytes
  ld    hl, #_stack_top      ;; 3 bytes
  ld    sp, hl               ;; 1 byte

  jr    init_continued       ;; 2 bytes

  ;; ========================================================================
  ;; RST 0x08 ENTRYPOINT: enc28j60_select_bank
  ;; ========================================================================

  .org  0x08

  ;; ------------------------------------------------------------------------
  ;; clear bits 0 and 1 of register ECON1
  ;; ------------------------------------------------------------------------

  ld   hl, #0x0100 * 0x03 + OPCODE_BFC + (ECON1 & REG_MASK)        ;; 3 bytes
  rst  enc28j60_write8plus8                                        ;; 1 byte

  ;; ------------------------------------------------------------------------
  ;; mask in "bank" in bits 0 and 1 of register ECON1
  ;; ------------------------------------------------------------------------

  ld    l, #OPCODE_BFS + (ECON1 & REG_MASK)                         ;; 2 bytes
  jr    enc28j60_select_bank_continued                              ;; 2 bytes

  ;; ========================================================================
  ;; RST 0x10 ENTRYPOINT: enc28j60_write_register16
  ;; ========================================================================

  .org   0x10

  ld     e, a
  inc    e
  ld     d, h

  ld     h, l
  ld     l, a

  rst   enc28j60_write8plus8

  ex     de, hl

  nop

  ;; FALL THROUGH to RST 0x18 == enc28j60_write8plus8

  ;; ========================================================================
  ;; RST 0x18 ENTRYPOINT: enc28j60_write8plus8
  ;; ========================================================================

  .org  0x18
  
  ld    c, l
  rst   spi_write_byte
  ld    c, h
  rst   spi_write_byte
  jp    enc28j60_end_transaction_and_return

  ;; ========================================================================
  ;; RST 0x20 ENTRYPOINT: spi_write_byte
  ;; ========================================================================
  
  .org  0x20
  
  ld    b, #8
  jp    spi_write_byte_cont

  ;; ========================================================================
  ;; RST 0x28 ENTRYPOINT: enc28j60_write_memory_small
  ;; ========================================================================

  .org  0x28
  
  ld    d, #0                                                      ;; 2 bytes
  jp    enc28j60_write_memory                                      ;; 3 bytes

  ;; --------------------------------------------------------------------------
  ;; continuation of enc28j60_select_bank (RST 0x08)
  ;; --------------------------------------------------------------------------

enc28j60_select_bank_continued:
  ld   h, e                                                        ;; 1 byte
  rst  enc28j60_write8plus8                                        ;; 1 byte
  ret                                                              ;; 1 byte

  ;; ========================================================================
  ;; RST 0x30 ENTRYPOINT: memory_compare
  ;; ========================================================================
  
  .org  0x30
 
 memory_compare_loop:
  ld   a, (de)
  cp   a, (hl)
  ret  nz
  inc  de
  inc  hl
  djnz memory_compare_loop
  ret

  ;; ========================================================================
  ;; RST 0x38 ENTRYPOINT: 50 Hz interrupt
  ;;
  ;; Increase 16-bit value at '_timer_tick_count' by 2
  ;; ========================================================================

  .org	0x38
  push  hl
  ld	  hl, (_timer_tick_count)
  inc	  hl
  inc	  hl
  ld	  (_timer_tick_count), hl
  pop   hl
  ei
  ret

  ;; ==========================================================================
  ;; continued initialization (from 0x0000)
  ;; ==========================================================================

init_continued:

  ;; --------------------------------------------------------------------------
  ;; Configure memory banks, and ensure ROM1 (BASIC ROM) is paged in.
  ;; This sequence differs between SpeccyBoot and DGBoot. The SpeccyBoot one
  ;; is longer (12 bytes).
  ;; --------------------------------------------------------------------------

#ifdef HWTARGET_SPECCYBOOT

  ;; before the 128k memory configuration is set (0x7ffd), set the
  ;; +2A/+3 memory configuration (0x1ffd). On a plain 128k machine,
  ;; the access to 0x1ffd would be mapped to 0x7ffd, overwriting the
  ;; 128k configuration. On a 48k machine neither access has any effect.
  
  ;; Set the ROM selection bit in both registers to page in the 48k
  ;; BASIC ROM (ROM1 on the 128k, ROM3 on +2A/+3).

  ;; The Didaktik doesn't use '128-style banking, so we save a few bytes
  ;; here. They are needed to keep the RST handlers below in place.

  ld    de, #0x0410
  ld    bc, #MEMCFG_PLUS_ADDR
  out   (c), d
  ld    b, #>MEMCFG_ADDR
  out   (c), e

#endif

#ifdef HWTARGET_DGBOOT

  ;; Initialize the Didaktik 8255  (8 bytes)
  CWR   = 0x7f

  ld    a, #0x20
  out   (SPI_OUT),a          ;; Question: why this needed? Pages out DGBoot???
  ld    a, #0x90             ;; PB out, PC out, PA in, mode 0
  out   (CWR), a

#endif

  ;; --------------------------------------------------------------------------
  ;; Copy trampoline to RAM. Far more than the trampoline is copied, since
  ;; this routine has an important side-effect: it provides a delay > 200ms
  ;; @3.5469MHz, for 128k reset logic to settle.

  ;; 200ms = 709380 T-states = 33780 (0x83F4) LDIR iterations. However,
  ;; since DE points to contended memory, each iteration will take longer
  ;; time than that in reality. Stick with this safe overkill.
  ;; --------------------------------------------------------------------------
  
do_copy_trampoline:

  ld    hl, #ram_trampoline
  ld    de, #_stack_top
  push  de
  ld    bc, #0x83F4

  ldir
  
  ret   ;; jump to _stack_top

  ;; --------------------------------------------------------------------------
  ;; Trampoline: copied to RAM above. Must be executed from RAM, since
  ;; it pages out the SpeccyBoot ROM.
  ;;
  ;; Copies font data to _font_data (defined in globals.inc). If CAPS SHIFT is
  ;; pressed, executes BASIC.
  ;; --------------------------------------------------------------------------

ram_trampoline:

  ld    a, #PAGE_OUT         ;; page out SpeccyBoot, keep ETH in reset
  out   (SPI_OUT), a

  ;; Is Caps Shift being pressed? Clear C flag if it is

  ld    a, #0xFE             ;; 0xFEFE: keyboard scan row CAPS..V
  in    a, (0xFE)
  rra

  jp    nc, 0              ;; if Caps Shift was pressed, go to BASIC

  ld    hl, #0x3d00        ;; address of font data in ROM1
  ld    de, #_font_data    ;; address of font buffer in RAM; means E is now 3
  ld    b, e               ;; BC is now 0x300  (BC was 0 after previous LDIR)
  ldir

  xor   a                  ;; page in SpeccyBoot ROM, keep ETH in reset
  out   (SPI_OUT), a

  jp    initialize_global_data

  ;; --------------------------------------------------------------------------
  ;; initialization of (mostly just clearing) global data
  ;; --------------------------------------------------------------------------

initialize_global_data:

  ;; clear RAM up to stage2_start + 1; this also sets screen to PAPER+INK 0
  ld    hl, #0x4000
  ld    de, #0x4001
  ld    bc, #stage2_start - 0x4000
  ld    (hl), a
  out   (ULA_PORT), a
  ldir

  ld    (_tftp_write_pos), hl

  ei

  ;; --------------------------------------------------------------------------
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------

  .area _CODE

  .area _DATA

  .area _STAGE2        ;; this is where the stage 2 bootloader starts (RAM)
  .area _NONRESIDENT   ;; continues here, with stuff that need not be resident
  .area _MAGIC         ;; area for magic number (integrity check)
  .area _SNAPSHOTLIST  ;; area for loaded snapshot list (snapshots.lst)

_tftp_file_buffer::

  .area _STAGE2

stage2_start:

  ;; --------------------------------------------------------------------------
  ;; Special mark for integrity check, as last three bytes in loaded binary:
  ;; entry point (2 bytes)
  ;; VERSION_MAGIC (magic number depending on version, 2 bytes)
  ;; --------------------------------------------------------------------------

  .area _MAGIC

  .dw   run_menu
  .dw   VERSION_MAGIC

  .area _SNAPSHOTLIST

_snapshot_list:
