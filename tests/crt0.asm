  ; crt0 for the test application: the resulting image expected to be
  ; loaded at address 0x6000

  .module crt0
  .globl	_main
  
  ; external variables for initial values of registers

  .area	_HEADER (ABS)
  .org 	0x7000
  init:
  push af
  ld  a, r
  push af
  ld  a, i
  push af
  push bc
  push de
  push hl
  push ix
  push iy
  ex af, af'
  push af
  exx
  push bc
  push de
  push hl

  jp  _main

  ;; Ordering of segments for the linker.
  .area	_HOME
  .area	_CODE
  .area   _GSINIT
  .area   _GSFINAL

  .area	_DATA
  .area   _BSS
  .area   _HEAP

  .area   _CODE

  .area   _GSINIT
  gsinit::

  .area   _GSFINAL
