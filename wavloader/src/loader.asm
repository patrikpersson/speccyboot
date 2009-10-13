;;
;; loader: Code to write a firmware image to FRAM. The firmware image is
;;         expected to be 16k in size, and located immediately after this
;;         loader in RAM.
;;
;; Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
;;
;; ----------------------------------------------------------------------------
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
;;
;; ============================================================================

  ;; FRAM control

  FRAM_OUT     = 0x28   ;; FRAM paged out, internal ROM enabled, ETH in reset
  FRAM_IN      = 0x08   ;; FRAM paged in, internal ROM disabled, ETH in reset
  FRAM_CTL     = 0x9f   ;; SpeccyBoot FRAM & SPI control register

  ;; Keyboard input

  KBD_ENT_ROW  = 0xbffe ;; keyboard row with ENTER key
  KBD_ENT_MASK = 0x01   ;; mask for ENTER key

  ;; Firmware parameters

  FW_SIZE      = 0x4000
  FW_DEST      = 0x0000

  ;; Spectrum ROM routines. Refer to
  ;;
  ;;  Ian Logan: 'Understanding Your Spectrum',
  ;;  Melbourne House (Publishers) Ltd., England 1982.

  CHANNEL_S    = 2      ;; channel S (screen)

  BEEPER       = 0x03b5
  CHAN_OPEN    = 0x1601
  PR_STRING    = 0x203c

  ;; ==========================================================================

  .area	_HEADER (ABS)
  
  .org 	0x8000

  ;; ensure the FRAM is paged out, so it is safe to set switches to enable it
  ;; (useful when FRAM contains garbage)
  
  ld    a, #FRAM_OUT
  out   (FRAM_CTL), a
  
  ld    a, #CHANNEL_S
  call  CHAN_OPEN

  ;; --------------------------------------------------------------------------  
  ;; Wait for Enter to be pressed, then released
  ;; --------------------------------------------------------------------------  

  ld    de, #msg_press_enter
  ld    bc, #msg_press_enter_end-msg_press_enter
  call  PR_STRING

  ld    bc, #KBD_ENT_ROW
wait_key_press:
  in    a, (c)
  and   #KBD_ENT_MASK
  jr    nz, wait_key_press

wait_key_release:
  in    a, (c)
  and   #KBD_ENT_MASK
  jr    z, wait_key_release

  ld    de, #msg_writing
  ld    bc, #msg_writing_end-msg_writing
  call  PR_STRING

  ;; --------------------------------------------------------------------------  
  ;; The following section is executed with FRAM paged in, disabled interrupts
  ;; --------------------------------------------------------------------------  

  di

  ld    a, #FRAM_IN
  out   (FRAM_CTL), a

  ;; perform the write
  
  ld    hl, #firmware_image
  ld    de, #FW_DEST
  ld    bc, #FW_SIZE
  ldir

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

  ld    a, #FRAM_OUT
  out   (FRAM_CTL), a

  ei

  ;; --------------------------------------------------------------------------  
  ;; Check result of verification, print message and make noise
  ;; --------------------------------------------------------------------------  

  jr    z, success

  ld    de, #msg_fail
  ld    bc, #msg_fail_end-msg_fail
  call  PR_STRING

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
  ;; Messages
  ;; --------------------------------------------------------------------------
  
msg_press_enter:
  .db   0x11, 0x00, 0x10, 0x07      ;; PAPER 0; INK 7
  .db   0x16, 0x02, 0x03            ;; AT 2,3
  .db   "S", "p", "e", "c", "c", "y", "B", "o", "o", "t", " ", "f", "i"
  .db   "r", "m", "w", "a", "r", "e", " ", "l", "o", "a", "d", "e", "r"
  .db   0x11, 0x07, 0x10, 0x00      ;; PAPER 7; INK 0
  .db   0x16, 0x06, 0x05            ;; AT 6,5
  .db   "p", "r", "e", "s", "s", " ", "E", "n", "t", "e", "r"
  .db   " ", "w", "h", "e", "n", " ", "r", "e", "a", "d", "y"
msg_press_enter_end:

msg_writing:
  .db   0x16, 0x06, 0x05            ;; AT 6,5
  .db   " ", "w", "r", "i", "t", "i", "n", "g", " ", "f", "i"
  .db   "r", "m", "w", "a", "r", "e", " ", ".", ".", ".", " "
msg_writing_end:

msg_ok:
  .db   0x16, 0x0a, 0x00            ;; AT 10,0
  .db   "O", "K", ":", " ", " ", "f", "i", "r", "m", "w", "a", "r", "e"
  .db   " ", "w", "r", "i", "t", "t", "e", "n", " ", "&", " ", "v", "e"
  .db   "r", "i", "f", "i", "e", "d"
msg_ok_end:
  
msg_fail:
  .db   0x16, 0x0a, 0x0c            ;; AT 10,12
  .db   0x12, 0x01                  ;; FLASH 1
  .db   "F", "A", "I", "L", "E", "D", "!"
  .db   0x12, 0x00                  ;; FLASH 0
msg_fail_end:
  
  ;; --------------------------------------------------------------------------  
  ;; Firmware image expected to follow immediately after
  ;; --------------------------------------------------------------------------
  
firmware_image:
