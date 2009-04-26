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
  ;; Set up interrupts, delay for 200ms (for 128k reset logic to settle)
  ;; --------------------------------------------------------------------------  
  
  .org 	0
  di
  im    1

  ld    sp, #0xffff       ;; TODO: find a better place for the stack
  
  ld    a, #2
  out   (0xfe), a         ;; set border red during delay
  
  ld    bc, #27284        ;; 27284 x 26 = 709384 T-states = 200ms @3.5469MHz
reset_delay_loop::        ;; each loop iteration is 6+4+4+12 = 26 T-states
  dec   bc
  ld    a, b
  or    c
  jr    nz, reset_delay_loop
  
  jp    gsinit            ;; run static initalizers

  ;; --------------------------------------------------------------------------  
  ;; RST 0x30 ENTRYPOINT
  ;;
  ;; Just jump to a handler defined elsewhere
  ;; --------------------------------------------------------------------------  

  .org	0x30
  jp    _rst30_handler

  ;; --------------------------------------------------------------------------  
  ;; RST 0x38 ENTRYPOINT (50HZ INTERRUPT)
  ;;
  ;; increase 8-bit value at '_timer_tick_count', saturate at 0xff
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
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------  
  .area	_HOME
  .area	_CODE
  .area _GSINIT
  .area _GSFINAL

  .area	_DATA
  .area _BSS
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
