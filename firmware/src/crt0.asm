  ;; crt0.s for SpeccyBoot
  ;;
  ;; Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
  ;; --------------------------------------------------------------------------
  ;;
  ;; Copyright (c) 2009, Patrik Persson
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

  .module crt0
  
  .globl	_main
  .globl	_timer_tick_count
  .globl	_stack_bottom
  .globl	_stack_top

  .area	_HEADER (ABS)
  
  .org 	0
  di

  xor   a
  out   (0x9f), a         ;; SPI: RST low
  
  ;; This LDIR har two purposes, and an important side-effect:
  ;;
  ;; - Paint stack with 0xA8, for stack-checking code to work
  ;; - Provide a delay > 200ms  @3.5469MHz, for 128k reset logic to settle
  ;; - Ensure HL has the value of 0xFFFF when the loop terminates (required
  ;;   by go_to_bank)
  
  ld    hl, #_stack_bottom
  ld    de, #_stack_bottom+1
  ld    bc, #0xA4FF
  ld    (hl), #0xA8
  ldir
  
  ;; If Caps Shift is being pressed, jump to BASIC
  
  ld    a, #0xFE          ;; 0xFEFE: keyboard scan row CAPS..V
  in    a, (0xFE)
  rra
  ld    a, #0x20          ;; page in standard ROM, keep ETH in reset
  jr    nc, go_to_bank

  ;; If Symbol shift is being pressed, jump to alternate EEPROM bank

  ld    a, #0x7F          ;; 0x7FFE: keyboard scan row SPACE..B
  in    a, (0xFE)
  rra
  rra
  ld    a, #0x10          ;; page in alternate EEPROM, keep ETH in reset
  jr    nc, go_to_bank

  ;; Set up interrupts, static variables, and stack
  
  ld    sp, #_stack_top
  call  gsinit
  im    1
  
  ei
  jp _main

  ;; Store the instruction "out (0x9F), a" at 0xFFFE, then jump there.

go_to_bank::
  ld    (hl), #0x9F
  dec   hl
  ld    (hl), #0xD3
  jp    (hl)

  ;; --------------------------------------------------------------------------  
  ;; RST 0x38 (50HZ INTERRUPT) ENTRYPOINT
  ;;
  ;; Increase 16-bit value at '_timer_tick_count'
  ;; --------------------------------------------------------------------------  

  .org	0x38
  push  hl
  ld	hl, (_timer_tick_count)
  inc	hl
  ld	(_timer_tick_count), hl
  pop   hl
  ei
  ret
		      
  ;; --------------------------------------------------------------------------  
  ;; Initial part of VRAM trampoline
  ;;
  ;; located adjacent to VRAM, to allow execution to continue directly into
  ;; VRAM
  ;; --------------------------------------------------------------------------
  
vram_trampoline_before::
  .org  0x3ffe
vram_trampoline_initial::
  out   (c), a

  ;; --------------------------------------------------------------------------  
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------  

  .area	_HOME
  .area	_CODE
  .area _GSINIT
  .area _GSFINAL

  .area	_DATA
  .area _BSS

end_of_data::

  .area _HEAP

  .area _CODE
  .area _GSINIT
  
gsinit::
  .area   _GSFINAL
  ret
  
end_of_code::
