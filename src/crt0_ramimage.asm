  ; SDCC's crt0 module, modified for an image to load in RAM of an emulator

  .module crt0
  .globl	_main

  .area	_HEADER (ABS)
  ;; Reset vector
  .org 	0x8000
  init:
  ;; Stack at the top of memory.
  ld	sp,#0xffff

  ;; Initialise global variables
  jp  gsinit

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
  ei
  jp	_main
