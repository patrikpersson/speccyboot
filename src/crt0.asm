  ;; crt0.s for SpeccyBoot
  ;;
  ;; Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
  ;; --------------------------------------------------------------------------

  .module crt0
  
  .globl	_main
  .globl	_rst30_handler
  .globl	_timer_tick_count

  .area	_HEADER (ABS)

  ;; --------------------------------------------------------------------------  
  ;; RESET VECTOR
  ;;
  ;; Set up interrupts, enter a sensible RESET state for the ENC28J60, delay
  ;; for 200ms (for 128k reset logic to settle)
  ;;
  ;; The CPU stack is placed at 0x5D00 (512 bytes after video RAM). This should
  ;; make stack overruns visible.
  ;; --------------------------------------------------------------------------  
  
  .org 	0
  di
  im    1

  ld    a, #0x08          ;; CS high, RST low
  out   (0x9f), a

  ld    sp, #0x5D00
  
  ld    a, #2
  out   (0xfe), a         ;; set border red during delay
  
  ld    bc, #27284        ;; 27284 x 26 = 709384 T-states = 200ms @3.5469MHz
reset_delay_loop::        ;; each loop iteration is 6+4+4+12 = 26 T-states
  dec   bc
  ld    a, b
  or    c
  jr    nz, reset_delay_loop

  ;; --------------------------------------------------------------------------  
  ;; Paint the stack
  ;; --------------------------------------------------------------------------  

  ld    a, #4
  out   (0xfe), a         ;; set border green while painting
  ld    hl, #0x5B00
  ld    de, #0x5B01
  ld    bc, #0x01FF
  ld    a, #0xAB
  ld    (hl), a
  ldir
  
  ld    a, #1
  out   (0xfe), a         ;; set border blue while running static initializers
  jp    gsinit

  ;; --------------------------------------------------------------------------  
  ;; RST 0x30 ENTRYPOINT
  ;;
  ;; Just jump to a handler defined elsewhere
  ;; --------------------------------------------------------------------------  

  .org	0x30
  jp    _rst30_handler

  ;; --------------------------------------------------------------------------  
  ;; RST 0x38 (50HZ INTERRUPT) ENTRYPOINT
  ;;
  ;; Increase 8-bit value at '_timer_tick_count', saturate at 0xff
  ;; --------------------------------------------------------------------------  

  .org	0x38
  push  af
  push  hl
  ld    hl, #_timer_tick_count
  ld    a, (hl)
  inc   a
  jr z, timer_50hz_saturated    ;; don't wrap around to zero
  ld    (hl), a
timer_50hz_saturated::
  pop   hl
  pop   af
  ei
  ret

  ;; --------------------------------------------------------------------------  
  ;; 8-bit constant table
  ;;
  ;; For loading F/F' registers in trampoline
  ;; --------------------------------------------------------------------------
  
  .org  0x3e00
  .db #0x00, #0x01, #0x02, #0x03, #0x04, #0x05, #0x06, #0x07, #0x08, #0x09
  .db #0x0a, #0x0b, #0x0c, #0x0d, #0x0e, #0x0f, #0x10, #0x11, #0x12, #0x13
  .db #0x14, #0x15, #0x16, #0x17, #0x18, #0x19, #0x1a, #0x1b, #0x1c, #0x1d
  .db #0x1e, #0x1f, #0x20, #0x21, #0x22, #0x23, #0x24, #0x25, #0x26, #0x27
  .db #0x28, #0x29, #0x2a, #0x2b, #0x2c, #0x2d, #0x2e, #0x2f, #0x30, #0x31
  .db #0x32, #0x33, #0x34, #0x35, #0x36, #0x37, #0x38, #0x39, #0x3a, #0x3b
  .db #0x3c, #0x3d, #0x3e, #0x3f, #0x40, #0x41, #0x42, #0x43, #0x44, #0x45
  .db #0x46, #0x47, #0x48, #0x49, #0x4a, #0x4b, #0x4c, #0x4d, #0x4e, #0x4f
  .db #0x50, #0x51, #0x52, #0x53, #0x54, #0x55, #0x56, #0x57, #0x58, #0x59
  .db #0x5a, #0x5b, #0x5c, #0x5d, #0x5e, #0x5f, #0x60, #0x61, #0x62, #0x63
  .db #0x64, #0x65, #0x66, #0x67, #0x68, #0x69, #0x6a, #0x6b, #0x6c, #0x6d
  .db #0x6e, #0x6f, #0x70, #0x71, #0x72, #0x73, #0x74, #0x75, #0x76, #0x77
  .db #0x78, #0x79, #0x7a, #0x7b, #0x7c, #0x7d, #0x7e, #0x7f, #0x80, #0x81
  .db #0x82, #0x83, #0x84, #0x85, #0x86, #0x87, #0x88, #0x89, #0x8a, #0x8b
  .db #0x8c, #0x8d, #0x8e, #0x8f, #0x90, #0x91, #0x92, #0x93, #0x94, #0x95
  .db #0x96, #0x97, #0x98, #0x99, #0x9a, #0x9b, #0x9c, #0x9d, #0x9e, #0x9f
  .db #0xa0, #0xa1, #0xa2, #0xa3, #0xa4, #0xa5, #0xa6, #0xa7, #0xa8, #0xa9
  .db #0xaa, #0xab, #0xac, #0xad, #0xae, #0xaf, #0xb0, #0xb1, #0xb2, #0xb3
  .db #0xb4, #0xb5, #0xb6, #0xb7, #0xb8, #0xb9, #0xba, #0xbb, #0xbc, #0xbd
  .db #0xbe, #0xbf, #0xc0, #0xc1, #0xc2, #0xc3, #0xc4, #0xc5, #0xc6, #0xc7
  .db #0xc8, #0xc9, #0xca, #0xcb, #0xcc, #0xcd, #0xce, #0xcf, #0xd0, #0xd1
  .db #0xd2, #0xd3, #0xd4, #0xd5, #0xd6, #0xd7, #0xd8, #0xd9, #0xda, #0xdb
  .db #0xdc, #0xdd, #0xde, #0xdf, #0xe0, #0xe1, #0xe2, #0xe3, #0xe4, #0xe5
  .db #0xe6, #0xe7, #0xe8, #0xe9, #0xea, #0xeb, #0xec, #0xed, #0xee, #0xef
  .db #0xf0, #0xf1, #0xf2, #0xf3, #0xf4, #0xf5, #0xf6, #0xf7, #0xf8, #0xf9
  .db #0xfa, #0xfb, #0xfc, #0xfd, #0xfe, #0xff

  ;; --------------------------------------------------------------------------  
  ;; Initial part of VRAM trampoline
  ;;
  ;; located adjacent to VRAM, to allow execution to continue directly into
  ;; VRAM
  ;; --------------------------------------------------------------------------
  
  .org  0x3ffe
  out   (0x9f), a

  ;; --------------------------------------------------------------------------  
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------  

  .area	_HOME
  .area	_CODE
  .area _GSINIT
  .area _GSFINAL

  .area	_DATA
  .area _BSS
  ;; this label allows us to check where the _DATA segment ends (by
  ;; looking in speccyboot.sym)
end_of_data::
  .area _HEAP

  .area _CODE
  .area _GSINIT
gsinit::

  .area   _GSFINAL

  ;; --------------------------------------------------------------------------  
  ;; Executed after static initializers
  ;; --------------------------------------------------------------------------  
  
  ei
  jp	_main
