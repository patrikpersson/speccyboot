  ;; crt0.s for SpeccyBoot
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

#include "spi_asm.h"

  .module crt0

  .include "include/spi.inc"
  .include "include/enc28j60.inc"
  .include "include/globals.inc"
  .include "include/udp_ip.inc"
  .include "include/util.inc"

  .globl	_font_data
  .globl	_main
  .globl	_stack_top
  .globl	_timer_tick_count
  .globl  _tftp_file_buffer
  .globl  _run_menu

  .globl	l__INITIALIZER
  .globl	s__INITIALIZED
  .globl	s__INITIALIZER

  ;; --------------------------------------------------------------------------

  .area	_HEADER (ABS)

  ;; --------------------------------------------------------------------------

  .org 	0

  im    1
  ld    hl, #_stack_top
  ld    sp, hl
  ex    de, hl
  push de                    ;; trick: use as destination for RET below

#ifdef HWTARGET_DGBOOT
  ;; Initialize the Didaktik 8255
  CWR   = 0x7f

  ld    a, #0x20
  out   (SPI_OUT),a          ;; Question: why this needed? Pages out DGBoot???
  ld    a, #0x90             ;; PB out, PC out, PA in, mode 0
  out   (CWR), a
#endif

  ;; Copy trampoline to RAM. Far more than the trampoline is copied, since
  ;; this routine has an important side-effect: it provides a delay > 200ms
  ;; @3.5469MHz, for 128k reset logic to settle.

  ;; 200ms = 709380 T-states = 33780 (0x83F4) LDIR iterations. However,
  ;; since DE points to contended memory, each iteration will take longer
  ;; time than that in reality. Stick with a safe overkill.

  ld  hl, #ram_trampoline
  ld  bc, #0x83F4
  ldir                       ;; DE points to _stack_top

  ;; Is Caps Shift being pressed? Clear C flag if it is

  ld    a, #0xFE             ;; 0xFEFE: keyboard scan row CAPS..V
  in    a, (0xFE)
  rra

  ret                        ;; jump to 'font_data_loader' copy in RAM

  ;; --------------------------------------------------------------------------
  ;; Trampoline: copied to RAM above. Must be executed from RAM, since
  ;; it pages out the SpeccyBoot ROM.
  ;;
  ;; Copies font data to _font_data (defined in globals.h). If the C flag is
  ;; clear, executes BASIC.
  ;; --------------------------------------------------------------------------

ram_trampoline::
  ld    a, #PAGE_OUT ;; page out SpeccyBoot, keep ETH in reset
  out   (SPI_OUT), a

  jp    nc, 0       ;; if Caps Shift was pressed, go to BASIC

#ifndef HWTARGET_DGBOOT
  ;; before the 128k memory configuration is set (0x7ffd), set the
  ;; +2A/+3 memory configuration (0x1ffd). On a plain 128k machine,
  ;; the access to 0x1ffd would be mapped to 0x7ffd, overwriting the
  ;; 128k configuration. On a 48k machine neither access has any effect.

  ;; Set the ROM selection bit in both registers to page in the 48k
  ;; BASIC ROM (ROM1 on the 128k, ROM3 on +2A/+3).

  ;; The Didaktik doesn't use '128-style banking, so we save a few bytes
  ;; here. They are needed to keep the interrupt handler below in place.

  ld    de, #0x0410
  ld    bc, #0x1ffd ;; MEMCFG_PLUS_ADDR
  out   (c), d      ;; page in ROM1 (48k BASIC)

  ld    b, #0x7f    ;; 0x7ffd = MEMCFG_ADDR
  out   (c), e      ;; page in ROM1 (48k BASIC)
#endif

  ld    hl, #0x3d00 ;; address of font data in ROM1
  ld    de, #_font_data ;; address of font buffer in RAM; means E is now 3
  ld    b, e  ;; BC is now 0x3FD (SpeccyBoot) or 0x300 (DGBoot), overkill is ok
  ldir

  xor   a           ;; page in SpeccyBoot, keep ETH in reset
  out   (SPI_OUT), a

  jp    gsinit

  ;; --------------------------------------------------------------------------
  ;; RST 0x38 (50HZ INTERRUPT) ENTRYPOINT
  ;;
  ;; Increase 16-bit value at '_timer_tick_count' by 2
  ;; --------------------------------------------------------------------------

  .org	0x38
  push  hl
  ld	  hl, (_timer_tick_count)
  inc	  hl
  inc	  hl
  ld	  (_timer_tick_count), hl
  pop   hl
  ei
  ret

  ;; --------------------------------------------------------------------------
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------

  .area	_HOME
  .area	_CODE
#ifndef STAGE2_IN_RAM
  .area _STAGE2        ;; this is where the stage 2 bootloader starts
  .area _NONRESIDENT   ;; continues here, with stuff that need not be resident
#endif
  .area _INITIALIZER
  .area _GSINIT
  .area _GSFINAL

  .area	_DATA
  .area _INITIALIZED
  .area _BSS

#ifdef STAGE2_IN_RAM
  .area _STAGE2        ;; this is where the stage 2 bootloader starts
  .area _NONRESIDENT   ;; continues here, with stuff that need not be resident
#endif

  .area _SNAPSHOTLIST  ;; area for loaded snapshot

end_of_data::
_tftp_file_buffer::

  .area _HEAP

  .area _CODE
  .area _GSINIT

gsinit::
  ;; clear RAM up to _font_data; this also sets screen to PAPER+INK 0
  ld hl, #0x4000
  ld de, #0x4001
  ld bc, #_font_data - 0x4001
  ld (hl), a
  out (0xFE), a
  ldir

  ld bc, #l__INITIALIZER
  ld de, #s__INITIALIZED
  ld hl, #s__INITIALIZER
  ldir

  .area _GSFINAL
  ei
  jp _main

end_of_code::

  .area _STAGE2
_stage2::
  jp  _run_menu

  .area _SNAPSHOTLIST
_snapshot_list::
