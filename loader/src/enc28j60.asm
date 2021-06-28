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

    ;;
    ;; register allocation:
    ;;
    ;;
    ;; primary bank (in spi_read_byte_to_memory)
    ;; -----------------------------------------
    ;; B   inner (bit) loop counter, always in range 0..8
    ;; DE  outer (byte) loop counter
    ;; HL  destination in RAM
    ;;
    ;; secondary bank (in loop)
    ;; ------------------------
    ;; BC  temp register for one term in sum above
    ;; DE  0 (zero)
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    exx
    ld    hl, (_ip_checksum)
    ld    de, #0

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; -----------------------------------------------------------------------
    ;; Each iteration (16 bits) takes 1391 T-states <=> 40kb/s
    ;; -----------------------------------------------------------------------

word_loop:

    call spi_read_byte_to_memory      ;; 17+655

    ld   c, a                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; the CALL NZ below is not taken,
    ;; and B == 0 in the checksum addition instead

    ;; take care not to modify Z flag
    ld   a, e                         ;; 4

    call nz, spi_read_byte_to_memory  ;; 17+655

    ld   b, a                         ;; 4

    ex   af, af'                      ;; 4
    adc  hl, bc                       ;; 15
    ex   af, af'                      ;; 4

    jr   nz, word_loop                ;; 12

    ;; -----------------------------------------------------------------------
    ;; end of payload: add the final carry to HL
    ;; -----------------------------------------------------------------------

    ex    af, af'
    adc   hl, de

    ld    (_ip_checksum), hl

    pop   hl                       ;; bring back original HL

    jr    do_end_transaction

;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (primary HL) and A.
;;
;; Primary HL is increased, DE is decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary DE == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    exx                            ;;  4

    ld   b, #8                     ;;  7
read_byte_loop:
    SPI_READ_BIT_TO  (hl)          ;; 63 * 8
    djnz read_byte_loop            ;; 13 * 7 + 8

    dec  de                        ;;  6
    ld   a, d                      ;;  4
    or   e                         ;;  4

    ld   a, (hl)                   ;;  7
    inc  hl                        ;;  6

    exx                            ;;  4

    ret                            ;; 10

                                   ;; 655 T-states


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

    ld    c, b      ;; BC is now zero
    adc   hl, bc    ;; final carry only (BC is zero here)

    ld    (_ip_checksum), hl

    ret


;; ############################################################################
;; enc28j60_read_register
;; ############################################################################

enc28j60_read_register:

    ;; ------------------------------------------------------------------------
    ;; start transaction: RCR
    ;; ------------------------------------------------------------------------

    ld    a, e
    and   a, #REG_MASK       ;; opcode RCR = 0x00
    ld    c, a
    rst   spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; for MAC and MII registers, read and ignore a dummy byte
    ;; ------------------------------------------------------------------------

    ld    a, e
    add   a, a   ;; bit 7 in descriptor set? then this is a MAC or MII register

    ld   b, #8
    jr   nc, 00001$
    ld   b, #16  ;; for MAC/MII registers, read 2 bytes, keep the last one
    
00001$:

    ld    a, #SPI_IDLE
    out   (SPI_OUT), a
    inc   a
    out   (SPI_OUT), a
    in    a, (SPI_IN)
    rra
    rl    c

    djnz 00001$

do_end_transaction:

    jp   enc28j60_end_transaction_and_return
