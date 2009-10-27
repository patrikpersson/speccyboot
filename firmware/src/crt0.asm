  ;; crt0.s for SpeccyBoot
  ;;
  ;; Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
  ;; --------------------------------------------------------------------------

  .module crt0
  
  .globl	_main
  .globl	_timer_tick_count
  .globl	_stack_bottom
  .globl	_stack_top

  .area	_HEADER (ABS)

  ;; --------------------------------------------------------------------------  
  ;; RESET VECTOR
  ;;
  ;; Set up interrupts, enter a sensible RESET state for the ENC28J60, delay
  ;; for 200ms (for 128k reset logic to settle)
  ;;
  ;; The CPU stack is placed at 0x5C00 (256 bytes after video RAM). This should
  ;; make stack overruns clearly visible by mere ocular inspection.
  ;; --------------------------------------------------------------------------  
  
  .org 	0
  di

  ld    a, #0x08          ;; reset ETH controller
  out   (0x9f), a         ;; CS high, RST low

  im    1
  
  ld    bc, #27284        ;; 27284 x 26 = 709384 T-states > 200ms @3.5469MHz
reset_delay_loop::        ;; each loop iteration is 6+4+4+12 = 26 T-states
  dec   bc
  ld    a, b
  or    c
  jr    nz, reset_delay_loop

  ;; If Caps Shift is being pressed, jump to BASIC
  ;; (we do this after the delay loop above, to make sure the top RAM page
  ;; is initialized properly)
  
  ld    a, #0xFE          ;; 0xFEFE: keyboard scan row CAPS..V
  in    a, (0xFE)         ;; avoid touching BC, assigned above, used below
  rra
  jr    nc, go_to_basic

  ld    hl, #_stack_bottom
  ld    de, #_stack_bottom+1
  dec   c                 ;; bc is zero after loop above => BC=0x00ff
  ld    (hl), #0xA8
  ldir                    ;; paint stack

  ld    sp, #_stack_top
  call  gsinit
  ei
  jp _main

  ;; Stores the instruction 'out (0x9F), a' at 0xFFFE, then jumps there.
  ;; The value 0x28 pages in the standard ROM (ROM0 on the 128), and keeps
  ;; ETH in reset.

go_to_basic::
  ld    hl, #0xFFFF
  ld    (hl), #0x9F
  dec   hl
  ld    (hl), #0xD3
  ld    a, #0x28
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
  ;; Various constant stuff, moved here to exploit the 'hole' between
  ;; the 8-bit constant table and the VRAM trampoline
  ;; --------------------------------------------------------------------------

  ;; --------------------------------------------------------------------------
  ;; Keyboard mapping (used by _poll_key below)
  ;:
  ;; ZX Spectrum BASIC Programming (Vickers), Chapter 23:
  ;;
  ;; IN 65278 reads the half row CAPS SHIFT to V
  ;; IN 65022 reads the half row A to G
  ;; IN 64510 reads the half row Q to T
  ;; IN 63486 reads the half row 1 to 5
  ;; IN 61438 reads the half row O to 6
  ;; IN 57342 reads the half row P to 7
  ;; IN 49150 reads the half row ENTER to H
  ;; IN 32766 reads the half row SPACE to B
  ;;
  ;; http://www.worldofspectrum.org/ZXBasicManual/index.html
  ;;
  ;; A '0' in the 'key_rows' table means that key is to be ignored. The rows
  ;; are ordered for the high byte in the row address to take values in the
  ;; following order:
  ;;
  ;; 01111111
  ;; 10111111
  ;; 11011111
  ;; 11101111
  ;; 11110111
  ;; 11111011
  ;; 11111101
  ;; 11111110
  ;; --------------------------------------------------------------------------

key_rows::
  .db 0x20, 0, 0x4d, 0x4e, 0x42     ;; 7FFE: space, shift, 'M', 'N', 'B'
  .db 13, 0x4c, 0x4b, 0x4a, 0x48    ;; BFFE: enter, 'L', 'K', 'J', 'H'
  .db 0x50, 0x4f, 0x49, 0x55, 0x59  ;; DFFE: 'P', 'O', 'I', 'U', 'Y'
  .db 0x30, 0x39, 0x38, 0x37, 0x36  ;; EFFE: '0', '9', '8', '7', '6'
  .db 0x31, 0x32, 0x33, 0x34, 0x35  ;; F7FE: '1', '2', '3', '4', '5'
  .db 0x51, 0x57, 0x45, 0x52, 0x54  ;; FBDE: 'Q', 'W', 'E', 'R', 'T'
  .db 0x41, 0x53, 0x44, 0x46, 0x47  ;; FDFE: 'A', 'S', 'D', 'F', 'G'
  .db 0, 0x5a, 0x58, 0x43, 0x56     ;; FEFE: shift, 'Z', 'X', 'C', 'V'

  ;; --------------------------------------------------------------------------
  ;; Keyboard polling routine
  ;; --------------------------------------------------------------------------
  
_poll_key::  
  ld    hl, #key_rows
  ld    bc, #0x7ffe
poll_outer::
  in    d, (c)
  
  ld    e, #5       ;; number of keys in each row
  
poll_inner::
  ld    a, (hl)
  inc   hl
  rr    d
  jr    c, not_pressed
  or    a
  jr    nz, poll_done
  
not_pressed::
  dec   e
  jr    nz, poll_inner
  
  rrc   b
  jr    c, poll_outer
  
  xor   a         ;; KEY_NONE == 0
  
poll_done::
  ld    l, a
  ret

  ;; --------------------------------------------------------------------------
  ;; Tiny sound, for a key click
  ;; --------------------------------------------------------------------------
_key_click::
  ld    bc, #0x14FE
  ld    d, #0x10
  ld    a, d
  di
keyclick_loop::
  out   (c), a
  xor   a, d
  djnz  keyclick_loop
  ei
  ret

  ;; --------------------------------------------------------------------------
  ;; Clear screen
  ;; --------------------------------------------------------------------------

_cls::
  ld  hl, #0x4000
  ld  de, #0x4001
  ld  bc, #0x1AFF
  ld  (hl), #0
  ldir
  ret

  ;; --------------------------------------------------------------------------
  ;; Print 8x8 pattern
  ;; --------------------------------------------------------------------------

_print_pattern_at::

  ;; assume row             at (sp + 2)
  ;;        col             at (sp + 3)
  ;;        LOBYTE(pattern) at (sp + 4)
  ;;        HIBYTE(pattern) at (sp + 5)
  ;;
  ;; use:
  ;;
  ;; de = destination in VRAM
  ;; hl = pattern
  
  ld    hl, #2
  add   hl, sp
  
  ;; compute d as 0x40 + (row & 0x18)
  
  ld    a, (hl)    ;; row
  and   a, #0x18
  add   a, #0x40
  ld    d, a
  
  ;; compute e as ((row & 7) << 5) + col
  
  ld    a, (hl)   ;; row
  inc   hl        ;; now points to col
  and   a, #7
  rrca            ;; rotate right by 3 == rotate left by 5, for values <= 7
  rrca
  rrca
  add   a, (hl)   ;; col
  inc   hl        ;; now points to LOBYTE(pattern)
  ld    e, a
    
  ld    c, (hl)
  inc   hl
  ld    h, (hl)
  ld    l, c
    
  ld    b, #8
print_pattern_at_loop::
  ld    a, (hl)
  ld    (de), a
  inc   d
  inc   hl
  djnz  print_pattern_at_loop
  
  ret
    
  ;; --------------------------------------------------------------------------
  ;; Digit font data (for progress display, used by util.c)
  ;; --------------------------------------------------------------------------

_digit_features::
  .db 0x77          ;;  0
  .db 0xa4          ;;  1
  .db 0x5e          ;;  2
  .db 0x6e          ;;  3
  .db 0x2d          ;;  4
  .db 0x6b          ;;  5
  .db 0x7b          ;;  6
  .db 0x26          ;;  7
  .db 0x7f          ;;  8
  .db 0x6f          ;;  9

_feature_rows::
  .db  4,      0, 0x01,    1, 0xff,    2, 0x80,    7, 0x40
  .db  2,      0, 0x01,                2, 0x80
  .db  5,      0, 0x01,    3, 0xff,    2, 0x80
  .db          4, 0x01,                5, 0x80
  .db  2,      4, 0x01,                5, 0x80
  .db  3,      4, 0x01,    6, 0xff,    5, 0x80

  ;; --------------------------------------------------------------------------
  ;; Various bitmaps for menu display (main.c)
  ;; --------------------------------------------------------------------------
  
_bottom_left_arc::
  .db 0, 0x80
_top_left_arc::
  .db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80
_bottom_right_arc::
  .db 0, 0x01
_top_right_arc::
  .db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01
_bottom_half::
  .db 0, 0
_top_half::
  .db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0
_offset_bottom::
  .db 0x1F, 0x0E
_offset_top::
  .db 0, 0, 0, 0, 0, 0, 0x0E
_offset_bar::
  .db 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F
_left_blob::
  .db 0x1F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x1F
_right_blob::
  .db 0xF8, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xF8
  
  ;; --------------------------------------------------------------------------  
  ;; Initial part of VRAM trampoline
  ;;
  ;; located adjacent to VRAM, to allow execution to continue directly into
  ;; VRAM
  ;; --------------------------------------------------------------------------
  
vram_trampoline_before::
  .org  0x3ffe
vram_trampoline_initial::
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

end_of_data::

  .area _HEAP

  .area _CODE
  .area _GSINIT
  
gsinit::
  .area   _GSFINAL
  ret
  
end_of_code::
