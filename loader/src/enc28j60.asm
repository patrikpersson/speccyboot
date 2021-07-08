;; Module enc28j60:
;;
;; Access to the Microchip ENC28J60 Ethernet host.
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

    .module enc28j60

    .include "enc28j60.inc"

    .include "eth.inc"
    .include "spi.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================

    .area _CODE

;; ############################################################################
;; enc28j60_read_memory_to_rxframe
;; ############################################################################

enc28j60_read_memory_to_rxframe:

    ld    hl, #_rx_frame

    ;; FALL THROUGH to enc28j60_read_memory


;; ############################################################################
;; enc28j60_read_memory
;; ############################################################################

enc28j60_read_memory:

    push   hl                      ;; preserve HL
    push   hl
    pop    ix
    push   de

    ;;
    ;; register allocation:
    ;;
    ;; IX  destination in RAM
    ;;
    ;;
    ;; primary bank (in spi_read_byte_to_memory)
    ;; -----------------------------------------
    ;; B   inner (bit) loop counter, zero outside subroutine
    ;; C   SPI_OUT
    ;; D   scratch
    ;; E   unused
    ;; L   SPI_IDLE
    ;; H   SPI_IDLE+SPI_SCK
    ;;
    ;; secondary bank (in loop)
    ;; ------------------------
    ;; BC  byte counter
    ;; D   return value from spi_read_byte_to_memory
    ;; DE  temp register for one term in sum above
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    ld   c, #SPI_OUT
    ld   hl, #0x0100 * (SPI_IDLE + SPI_SCK) + SPI_IDLE

    exx

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    pop   bc    ;; byte counter
    ld    hl, (_ip_checksum)

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; -----------------------------------------------------------------------
    ;; Each iteration (16 bits) takes 1081 T-states <=> ~ 51.8 kbit/s
    ;;
    ;; Inlining two bits more (2x4 instead of 4x2) would result in
    ;;   22 bytes more code (2xREAD_BIT_TO_D)
    ;;   26 T-states less per byte -> 1029 T-states in total <=> 54.4 kbit/s
    ;;
    ;; Inlining all 8 bits would result in
    ;;   66 bytes more code (6xREAD_BIT_TO_D), and a few less
    ;;   47+16+7 T-states less per byte (no B, no DJNZ, using HL instead of DE)
    ;;     -> 941 T-states in total <=> 59.5 kbit/s
    ;; -----------------------------------------------------------------------

word_loop:

    call spi_read_byte_to_memory      ;; 17+499

    ld   e, d                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; the CALL NZ below is not taken,
    ;; and D == 0 in the checksum addition instead

    ld   d, b                         ;; 4      D := 0, preserve Z flag

    call nz, spi_read_byte_to_memory  ;; 17+499

    ex   af, af'                      ;; 4
    adc  hl, de                       ;; 15
    ex   af, af'                      ;; 4

    jr   nz, word_loop                ;; 12

    ;; -----------------------------------------------------------------------
    ;; end of payload: add the final carry to HL
    ;; -----------------------------------------------------------------------

    ex    af, af'
    call  add_final_carry_and_store_checksum

    pop   hl                       ;; bring back original HL

do_end_transaction:

    jp   enc28j60_end_transaction_and_return


;; ----------------------------------------------------------------------------
;; Macro for spi_read_byte_to_memory below. Reads one bit from SPI to
;; register D. Requires  C==SPI_OUT,  L==SPI_IDLE  and  H==SPI_IDLE+SPI_SCK.
;; ----------------------------------------------------------------------------

   .macro READ_BIT_TO_D

    out   (c), l         ;; 12
    out   (c), h         ;; 12
    in    a, (SPI_IN)    ;; 11
    rra                  ;;  4
    rl    d              ;;  8,   total 47

   .endm

;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (IX) and D (primary+secondary).
;;
;; IX is increased, primary BC decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary BC == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    exx                               ;;  4

    ld    b, #4                       ;;  7
byte_read_loop:
    READ_BIT_TO_D
    READ_BIT_TO_D        ;; 47 * 8     = 376
    djnz  byte_read_loop ;; 13 * 3 + 8 = 47

    ld   (ix), d                      ;; 19
    inc  ix                           ;; 10

    ld   a, d                         ;;  4

    exx                               ;;  4

    ld   d, a                         ;;  4

    dec  bc                           ;;  6
    ld   a, b                         ;;  4
    or   c                            ;;  4

    ret                               ;; 10

                                      ;; 499 T-states


;; ############################################################################
;; enc28j60_add_to_checksum
;; ############################################################################

enc28j60_add_to_checksum:

    ld    hl, (_ip_checksum)

enc28j60_add_to_checksum_hl:

    or    a, a         ;; clear addition carry

checksum_loop:

    ld    a, (de)
    adc   a, l
    ld    l, a
    inc   de
    ld    a, (de)
    adc   a, h
    ld    h, a
    inc   de

    djnz checksum_loop

    ;; ------------------------------------------------------------------------
    ;; useful subroutine for enc28j60_read_memory above; requires B==0
    ;; ------------------------------------------------------------------------

add_final_carry_and_store_checksum:

    ld    c, b      ;; BC is now zero
    adc   hl, bc    ;; final carry only (BC is zero here)

    ld    (_ip_checksum), hl

    ret


;; ############################################################################
;; enc28j60_read_register
;; ############################################################################

enc28j60_read_register:

    ;; ------------------------------------------------------------------------
    ;; start transaction: RCR = 0x00
    ;; ------------------------------------------------------------------------

    ld    c, e
    rst   spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; Set B to either 8 or 16, since reading MAC and MII registers requires
    ;; ignoring a dummy byte
    ;; ------------------------------------------------------------------------

    ld   b, d
    call read_bits_to_c

    jr   do_end_transaction


;; ############################################################################
;; spi_read_byte_to_c
;; ############################################################################

spi_read_byte_to_c:
    ld   b, #8

    ;; FALL THROUGH to read_bits_to_c

;; ===========================================================================
;; helper: read_bits_to_c
;;
;; Reads B (typically 8) bits into C. Destroys AF, returns with B==0.
;; ===========================================================================

read_bits_to_c:

    SPI_READ_BIT_TO   c
    djnz  read_bits_to_c
    ret