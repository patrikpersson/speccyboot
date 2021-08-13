;;
;; loader: Code to write a firmware image to EEPROM. The firmware image is
;;         expected to be located immediately after this loader in RAM.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ---------------------------------------------------------------------------
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
;; ===========================================================================

  ;; ROM switching control. EEPROM pages below are 16K halves of a 32K EEPROM,
  ;; paged using bit 4 of the control register. Page 0 is the default one.

  EEPROM_OUT      = 0x28  ;; EEPROM paged out, internal ROM enabled, ETH reset
  EEPROM_IN       = 0x08  ;; EEPROM paged in, internal ROM disabled, ETH reset
  EEPROM_IN_PG0   = EEPROM_IN
  EEPROM_IN_PG1   = 0x18

  SPI_CS   = 0x08
  SPI_RST  = 0x40

  PAGE_OUT = 0x20       ;; mask for paging out SpeccyBoot, paging in BASIC ROM

  SPI_IDLE = PAGE_OUT + SPI_RST ;; SPI idle, MOSI=0, RST high, CS low, SCK low

  SPI_CTL         = 0x9f   ;; SpeccyBoot EEPROM & SPI control register

  ;; EEPROM write protection config
  
  CONFIG_28C16    = 0x31      ;; '1'
  CONFIG_28C64    = 0x32      ;; '2'
  CONFIG_28C256   = 0x33      ;; '3'
  CONFIG_PLAIN    = 0x34      ;; '4'
  
  ;; EEPROM parameters (64 pages, each 32 bytes)

  EEPROM_PAGESIZE = 0x20
  NBR_PAGES       = 64

  ;; Keyboard input

  KBD_12345_ROW   = 0xf7fe
  KBD_09876_ROW   = 0xeffe

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
  CLS             = 0x0db6
  CHAN_OPEN       = 0x1601
  PR_STRING       = 0x203c
  STACK_BC        = 0x2d2b
  PRINT_FP        = 0x2de3


  ;; ==========================================================================

  .area	_CODE

  ;; ensure the EEPROM is paged out, so it is safe to set switches to enable it
  ;; (useful when EEPROM contains garbage)

restart:

  ld    a, #EEPROM_OUT
  out   (SPI_CTL), a
  
  call  CLS

  ld    a, #CHANNEL_S
  call  CHAN_OPEN

  ;; --------------------------------------------------------------------------  
  ;; Wait for a key (1, 2, 3) to be pressed
  ;; --------------------------------------------------------------------------  

  ld    de, #msg_press_key
  ld    bc, #msg_press_key_end-msg_press_key
  call  PR_STRING

wait_key_press:
  ld    bc, #KBD_12345_ROW
  in    a, (c)
  rra
  ld    d, #CONFIG_28C16
  jp nc, reprogram
  rra
  ld    d, #CONFIG_28C64
  jp nc, reprogram
  rra
  ld    d, #CONFIG_28C256
  jp nc, reprogram
  rra
  ld    d, #CONFIG_PLAIN
  jp nc, reprogram

  ld    b, #>KBD_09876_ROW
  in    a, (c)
  rra
  jr    c, wait_key_press

  ;; =========================================================================
  ;; HARDWARE TEST
  ;; =========================================================================

  ld    a, #'0'
  rst   #PRINT_A

  ;; -------------------------------------------------------------------------
  ;; reset ENC28J60
  ;; -------------------------------------------------------------------------

  xor  a, a                      ;; RST low
  out  (SPI_CTL), a
  ld   a, #SPI_IDLE              ;; RST high again
  out  (SPI_CTL), a

  ;; -------------------------------------------------------------------------
  ;; write EWRPTH := 0x12
  ;; -------------------------------------------------------------------------

  ld   c, #0x43                                   ;; WCR = 0x40, EWRPTH = 0x03
  call spi_write_byte
  ld   c, #0x12
  call spi_write_byte
  call spi_end_transaction

  ;; -------------------------------------------------------------------------
  ;; write EWRPTL := 0x34
  ;; -------------------------------------------------------------------------

  ld   c, #0x42                                   ;; WCR = 0x40, EWRPTL = 0x02
  call spi_write_byte
  ld   c, #0x34
  call spi_write_byte
  call spi_end_transaction

  ;; -------------------------------------------------------------------------
  ;; read EWRPTH and EWRPTL
  ;; -------------------------------------------------------------------------

  ld   c, #0x03                                    ;; RCR = 0x0, EWRPTH = 0x03
  call spi_write_byte
  call spi_read_byte_and_end_transaction

  ld   h, c                                                ;; keep ERDPTH in H

  ld   c, #0x02                                    ;; RCR = 0x0, EWRPTH = 0x02
  call spi_write_byte
  call spi_read_byte_and_end_transaction

  ld   l, c

  ;; -------------------------------------------------------------------------
  ;; select bank 3
  ;; -------------------------------------------------------------------------

  ld   c, #0x80 | 0x1f                             ;; BFS = 0x80, ECON1 = 0x1f
  call spi_write_byte
  ld   c, #0x03                                    ;; set bits 0..1, bank := 3
  call spi_write_byte
  call spi_end_transaction

  ;; -------------------------------------------------------------------------
  ;; read EREVID
  ;; -------------------------------------------------------------------------

  ld   c, #0x12                                    ;; RCR = 0x0, EREVID = 0x12
  call spi_write_byte
  call spi_read_byte_and_end_transaction

  ;; -------------------------------------------------------------------------
  ;; check the EWRPTH/L values read
  ;; -------------------------------------------------------------------------

  ld   de, #0x1234
  or   a, a                                                     ;; clear carry
  sbc  hl, de

  jr    nz, spi_check_failed

  push  bc                                         ;; keep C == HW revision ID

  ld    de, #msg_spi_ok
  ld    bc, #msg_spi_ok_end-msg_spi_ok

  call  PR_STRING

  pop   bc
  push  bc

  ;; -------------------------------------------------------------------------
  ;; print '1' or '0', depending on whether bit 4 in C is set
  ;; -------------------------------------------------------------------------

  ld    a, #'0'
  bit   4, c
  jr    z, print0
  inc   a
print0:
  rst   #PRINT_A

  ;; -------------------------------------------------------------------------
  ;; translate lower 4 bits of version ID to a hex digit
  ;; -------------------------------------------------------------------------

  pop   bc
  ld    a, c
  and   a, #0x0f
  add   a, #0x30
  cp    a, #0x3a               ;; letter or digit?
  jr    c, digit
  add   a, #'A' - ('0' + 10)
digit:
  rst   #PRINT_A

  jr    wait_key_and_restart

spi_check_failed:

  ld    de, #msg_spi_fail
  ld    bc, #msg_spi_fail_end-msg_spi_fail
  call  PR_STRING

  ;; =========================================================================
  ;; WAIT FOR KEYPRESS, THEN RESTART
  ;; =========================================================================

wait_key_and_restart:

  ld    de, #msg_press_key_to_restart
  ld    bc, #msg_press_key_to_restart_end-msg_press_key_to_restart

  call  PR_STRING

  ld    bc, #<KBD_12345_ROW                          ;; scan all keys (B == 0)

  ;; -------------------------------------------------------------------------
  ;; wait until all keys are released
  ;; -------------------------------------------------------------------------

wait_any_key0:
  in    a, (c)
  cpl
  and   a, #0x1f
  jr    nz, wait_any_key0

  ;; -------------------------------------------------------------------------
  ;; wait for any key to be pressed
  ;; -------------------------------------------------------------------------

wait_any_key1:
  in    a, (c)
  cpl
  and   a, #0x1f
  jr    z, wait_any_key1

  ;; -------------------------------------------------------------------------
  ;; wait until all keys are released
  ;; -------------------------------------------------------------------------

wait_any_key2:
  in    a, (c)
  cpl
  and   a, #0x1f
  jr    nz, wait_any_key2

  jp    restart

  ;; =========================================================================
  ;; FIRMWARE PROGRAMMING
  ;; =========================================================================

reprogram:
  ld    a, d
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
  out   (SPI_CTL), a

  ;; perform the write in chunks of EEPROM_PAGESIZE
  
  ld    hl, #firmware_image
  ld    de, #FW_DEST

  ld    a, #NBR_PAGES / 2

  ld    bc, #0x5800 + 16 * 32       ;; attribute line 16

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
  out   (SPI_CTL), a

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
  call  BEEPER

  jp    wait_key_and_restart

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
  out   (SPI_CTL), a

  ld    a, #0xaa
  ld    (0x1555), a    ;; page 1 enabled => address on bus is 0x5555
  call  write_delay

  ld    a, #EEPROM_IN_PG0
  out   (SPI_CTL), a
  
  ld    a, #0x55
  ld    (0x2aaa), a
  call  write_delay

  ld    a, #EEPROM_IN_PG1
  out   (SPI_CTL), a

  ld    a, #0xaa
  ld    (0x1555), a    ;; page 1 enabled => address on bus is 0x5555
  call  write_delay

  ld    a, #EEPROM_IN_PG0
  out   (SPI_CTL), a

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
  ;; spi_write_byte: writes SPI byte in C
  ;; --------------------------------------------------------------------------

spi_write_byte:

  ld    b, #8
spi_write_byte_loop:

  ld    a, #SPI_IDLE+SPI_IDLE
  sla   c
  rra
  out   (SPI_CTL), a
  inc   a
  out   (SPI_CTL), a

  djnz  spi_write_byte_loop
  ret

  ;; --------------------------------------------------------------------------  
  ;; spi_read_byte: reads SPI byte into C, then end transaction
  ;; --------------------------------------------------------------------------

spi_read_byte_and_end_transaction:

  ld    b, #8
spi_read_byte_loop:

  ld    a, #SPI_IDLE
  out   (SPI_CTL), a
  inc   a
  out   (SPI_CTL), a
  in    a, (SPI_CTL)
  rra
  rl    c

  djnz  spi_read_byte_loop

  ;; FALL THROUGH to spi_end_transaction

  ;; --------------------------------------------------------------------------  
  ;; spi_end_transaction
  ;; --------------------------------------------------------------------------

spi_end_transaction:

  ld  a, #SPI_IDLE
  out (SPI_CTL), a
  ld  a, #SPI_IDLE+SPI_CS
  out (SPI_CTL), a

  ret

  ;; --------------------------------------------------------------------------  
  ;; Messages
  ;; --------------------------------------------------------------------------
  
msg_press_key:
  .db   0x14, 1, 0x13, 1      ;; INVERSE 1, BRIGHT 1
  .ascii " SpeccyBoot firmware installer  "
  .db   0x14, 0, 0x13, 0      ;; INVERSE 0, BRIGHT 0
  .db   13, 13, 13

  .ascii " 0. test SPI/ENC28J60 connection"
  .db   13, 13

  .ascii "or select EEPROM configuration:"
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
  .db   0x11, 7, 0x13, 0      ;; INK 7, BRIGHT 0
msg_ok_end:
  
msg_fail:
  .db   13, 13
  .ascii "     "
  .db   0x10, 6, 0x11, 2, 0x13, 1, 0x12, 1      ;; INK 6, PAPER 2, BRIGHT 1, FLASH 1
  .ascii " FAILED "
  .db   0x10, 0, 0x11, 7, 0x13, 0, 0x12, 0      ;; INK 0, PAPER 7, BRIGHT 0, FLASH 0
  .ascii " at address "
msg_fail_end:

msg_spi_ok:
  .db   13, 13, 13
  .ascii "SPI connection              "
  .db   0x11, 4, 0x13, 1                        ;; PAPER 4, BRIGHT 1
  .ascii " OK "
  .db   0x11, 7, 0x13, 0                        ;; PAPER 7, BRIGHT 0
  .db   13, 13
  .ascii "  detected ENC28J60 hw rev. "
  .db   0x13, 1                                 ;; BRIGHT 1
  .ascii "0x"
msg_spi_ok_end:

msg_spi_fail:
  .db   13, 13, 13, 13
  .ascii "SPI check "
  .db   0x10, 6, 0x11, 2, 0x13, 1, 0x12, 1      ;; INK 6, PAPER 2, BRIGHT 1, FLASH 1
  .ascii "FAILED"
  .db   0x10, 0, 0x11, 7, 0x13, 0, 0x12, 0      ;; INK 0, PAPER 7, BRIGHT 0, FLASH 0
msg_spi_fail_end:

msg_press_key_to_restart:
  .db   0x16, 21, 8                             ;; AT (21, 8)
  .db   0x12, 1, 0x13, 1                        ;; FLASH 1, BRIGHT 1
  .ascii " PRESS ANY KEY "
  .db   0x12, 0, 0x13, 0                        ;; FLASH 0, BRIGHT 0
msg_press_key_to_restart_end:

  ;; --------------------------------------------------------------------------  
  ;; Selected configuration
  ;; --------------------------------------------------------------------------

config:
  .db    0

  ;; --------------------------------------------------------------------------  
  ;; Firmware image expected to follow immediately after
  ;; --------------------------------------------------------------------------
  
firmware_image:
