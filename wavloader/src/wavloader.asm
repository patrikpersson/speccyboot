;;
;; loader: Code to write a firmware image to EEPROM. The firmware image is
;;         expected to be located immediately after this loader in RAM.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
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
;;
;; ============================================================================

  ;; ROM switching control. EEPROM pages below are 16K halves of a 32K EEPROM,
  ;; paged using bit 4 of the control register. Page 0 is the default one.

  EEPROM_OUT      = 0x28   ;; EEPROM paged out, internal ROM enabled, ETH reset
  EEPROM_IN       = 0x08   ;; EEPROM paged in, internal ROM disabled, ETH reset
  EEPROM_IN_PG0   = EEPROM_IN
  EEPROM_IN_PG1   = 0x18
  ROMCS_CTL       = 0x9f   ;; SpeccyBoot EEPROM & SPI control register

  ;; EEPROM write protection config
  
  CONFIG_28C16    = 0x31      ;; '1'
  CONFIG_28C64    = 0x32      ;; '2'
  CONFIG_28C256   = 0x33      ;; '3'
  CONFIG_PLAIN    = 0x34      ;; '4'
  
  ;; EEPROM parameters (64 pages, each 32 bytes)

  EEPROM_PAGESIZE = 0x20
  NBR_PAGES       = 64

  ;; Keyboard input

  KBD_DIGITS_ROW  = 0xf7fe ;; keyboard row with keys 1..3

  ;; Firmware parameters

  FW_SIZE         = 0x0800
  FW_DEST         = 0x0000

  ;; Spectrum ROM routines. Refer to
  ;;
  ;;  Ian Logan: 'Understanding Your Spectrum',
  ;;  Melbourne House (Publishers) Ltd., England 1982.

  CHANNEL_S       = 2      ;; channel S (screen)

  PRINT_A         = 0x10
  
  BEEPER          = 0x03b5
  CHAN_OPEN       = 0x1601
  PR_STRING       = 0x203c
  STACK_BC        = 0x2d2b
  PRINT_FP        = 0x2de3


  ;; ==========================================================================

  .area	_CODE

  ;; ensure the EEPROM is paged out, so it is safe to set switches to enable it
  ;; (useful when EEPROM contains garbage)
  
  ld    a, #EEPROM_OUT
  out   (ROMCS_CTL), a
  
  ld    a, #CHANNEL_S
  call  CHAN_OPEN

  ;; --------------------------------------------------------------------------  
  ;; Wait for a key (1, 2, 3) to be pressed
  ;; --------------------------------------------------------------------------  

  ld    de, #msg_press_key
  ld    bc, #msg_press_key_end-msg_press_key
  call  PR_STRING

  ld    bc, #KBD_DIGITS_ROW
wait_key_press:
  in    a, (c)
  rra
  jr nc, pressed1
  rra
  jr nc, pressed2
  rra
  jr nc, pressed3
  rra
  jr c, wait_key_press

  ld    a, #CONFIG_PLAIN
  jr    set_config

pressed1:
  ld    a, #CONFIG_28C16
  jr    set_config

pressed2:
  ld    a, #CONFIG_28C64
  jr    set_config

pressed3:
  ld    a, #CONFIG_28C256

set_config:
  ld    (config), a
  rst   #PRINT_A

  ld    de, #msg_writing
  ld    bc, #msg_writing_end-msg_writing
  call  PR_STRING

  ;; --------------------------------------------------------------------------
  ;; Following section is executed with EEPROM paged in, disabled interrupts
  ;; -------------------------------------------------------------------------- 

  di

  ld    a, #EEPROM_IN
  out   (ROMCS_CTL), a

  ;; perform the write in chunks of EEPROM_PAGESIZE
  
  ld    hl, #firmware_image
  ld    de, #FW_DEST

  ld    a, #NBR_PAGES / 2

  ld    bc, #0x5800 + 14 * 32       ;; attribute line 14

write_block_loop:

  ex    af, af'
  push  bc

  call  write_block
  call  write_block

  pop   bc

  ld    a, #0x78
  ld    (bc), a
  inc   bc

  ex    af, af'
  dec   a
  jr    nz, write_block_loop

  ;; verify write operation
  
  ld    hl, #firmware_image
  ld    de, #FW_DEST
  ld    bc, #FW_SIZE
verify_loop:
  ld    a, (de)
  inc   de
  cpi
  jr    nz, verify_done
  ld    a, b
  or    c
  jr    nz, verify_loop

verify_done:

  ld    a, #EEPROM_OUT
  out   (ROMCS_CTL), a

  ei
  
  ;; --------------------------------------------------------------------------  
  ;; Check result of verification, print message and make noise
  ;; --------------------------------------------------------------------------  

  jr    z, success

  dec   de                  ;; DE was increased after comparison
  push  de

  ld    de, #msg_fail
  ld    bc, #msg_fail_end-msg_fail
  call  PR_STRING

  pop   bc
  call  STACK_BC            ;; failed address now on calculator stack
  call  PRINT_FP

  ld    hl, #0x0CF2         ;; C (130.815 Hz)
  jr    do_beep

success:
  ld    de, #msg_ok
  ld    bc, #msg_ok_end-msg_ok
  call  PR_STRING

  ld    hl, #0x0184         ;; C (1046.52 Hz)
  
do_beep:
  ld    de, #0x0105         ;; 4s @1046.52Hz, 0.5s @130.815Hz
  jp    BEEPER

  ;; --------------------------------------------------------------------------  
  ;; Write one block (32 bytes)
  ;; --------------------------------------------------------------------------  

write_block:

  ld    a, (config)
  cp    #CONFIG_PLAIN
  jr    z, wp_done
  cp    #CONFIG_28C64
  jr    z, wp_28c64
  cp    #CONFIG_28C256
  jr    z, wp_28c256

  ;; Perform the special write-lock sequence supported by some 2K EEPROMs

  ld    a, #0xaa
  ld    (0x555), a
  call  write_delay
  
  ld    a, #0x55
  ld    (0x02aa), a
  call  write_delay
  
  ld    a, #0xa0
  ld    (0x555), a
  call  write_delay
  jr    wp_done

wp_28c64:

  ;; Perform the special write-lock sequence supported by some 8K EEPROMs

  ld    a, #0xaa
  ld    (0x1555), a
  call  write_delay
  
  ld    a, #0x55
  ld    (0x0aaa), a
  call  write_delay
  
  ld    a, #0xa0
  ld    (0x1555), a
  call  write_delay
  jr    wp_done

wp_28c256:
  ;; Perform the special write-lock sequence supported by some 32K EEPROMs

  ld    a, #EEPROM_IN_PG1
  out   (ROMCS_CTL), a

  ld    a, #0xaa
  ld    (0x1555), a    ;; page 1 enabled => address on bus is 0x5555
  call  write_delay

  ld    a, #EEPROM_IN_PG0
  out   (ROMCS_CTL), a
  
  ld    a, #0x55
  ld    (0x2aaa), a
  call  write_delay

  ld    a, #EEPROM_IN_PG1
  out   (ROMCS_CTL), a

  ld    a, #0xaa
  ld    (0x1555), a    ;; page 1 enabled => address on bus is 0x5555
  call  write_delay

  ld    a, #EEPROM_IN_PG0
  out   (ROMCS_CTL), a

wp_done:
  ld    bc, #EEPROM_PAGESIZE
  ldir

  ;; FALL THROUGH to write_delay

  ;; --------------------------------------------------------------------------  
  ;; Delay for enough time for a write operation to complete (> 5ms)
  ;; --------------------------------------------------------------------------  
  
  ;; 683 iterations x 26 T-states = 17758 T-states > 5ms @3.5469MHz

write_delay:
  ld    bc, #683
write_delay_loop:
  dec   bc
  ld    a, b
  or    c
  jr    nz, write_delay_loop

  ret

  ;; --------------------------------------------------------------------------  
  ;; Messages
  ;; --------------------------------------------------------------------------
  
msg_press_key:
  .db   0x14, 1, 0x13, 1      ;; INVERSE 1, BRIGHT 1
  .ascii " SpeccyBoot firmware installer  "
  .db   0x14, 0, 0x13, 0      ;; INVERSE 0, BRIGHT 0
  .db   13, 13, 13
  .ascii "select EEPROM configuration:"
  .db   13, 13
  .ascii  " 1. 28C16  with write protection"
  .db   13
  .ascii  " 2. 28C64  with write protection"
  .db   13
  .ascii  " 3. 28C256 with write protection"
  .db   13
  .ascii  " 4. plain, no write protection"
  .db   13, 13
  .ascii  ">> "
  .db   0x12, 1, ' ', 0x12, 0, 0x08
msg_press_key_end:

msg_writing:
  .db   13, 13, 13, 13
  .ascii "programming EEPROM...       "
msg_writing_end:

msg_ok:
  .db   0x11, 4, 0x13, 1      ;; INK 4, BRIGHT 1
  .ascii " OK "
msg_ok_end:
  
msg_fail:
  .db   13, 13
  .ascii "     "
  .db   0x10, 6, 0x11, 2, 0x13, 1, 0x12, 1      ;; INK 6, PAPER 2, BRIGHT 1, FLASH 1
  .ascii " FAILED "
  .db   0x10, 0, 0x11, 7, 0x13, 0, 0x12, 0      ;; INK 0, PAPER 7, BRIGHT 0, FLASH 0
  .ascii " at address "
msg_fail_end:
  
  ;; --------------------------------------------------------------------------  
  ;; Selected configuration
  ;; --------------------------------------------------------------------------

config:
  .db    0

  ;; --------------------------------------------------------------------------  
  ;; Firmware image expected to follow immediately after
  ;; --------------------------------------------------------------------------
  
firmware_image:
