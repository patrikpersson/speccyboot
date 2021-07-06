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
    ;; DE  outer (byte) loop counter
    ;; HL  destination in RAM
    ;;
    ;; secondary bank (in loop)
    ;; ------------------------
    ;; B   inner (bit) loop counter, zero outside subroutine
    ;; C   constant SPI_IDLE
    ;; D   result from spi_read_byte_to_memory
    ;; DE  temp register for one term in sum above
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    exx
    ld    hl, (_ip_checksum)

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; -----------------------------------------------------------------------
    ;; Each iteration (16 bits) takes 1241 T-states <=> ~ 45kb/s
    ;; -----------------------------------------------------------------------

word_loop:

    call spi_read_byte_to_memory      ;; 17+582

    ld   e, d                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; the CALL NZ below is not taken,
    ;; and D == 0 in the checksum addition instead

    ld   d, b                         ;; 4      D := 0, preserve Z flag

    call nz, spi_read_byte_to_memory  ;; 17+582

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
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (primary HL) and secondary D.
;;
;; Primary HL is increased, primary DE decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary DE == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    ;; B := 8 ; C := SPI_IDLE

    ld   bc, #8 * 0x0100 + SPI_IDLE   ;; 10
loop:
    ld    a, c           ;;  4
    out   (SPI_OUT), a   ;; 11
    inc   a              ;;  4
    out   (SPI_OUT), a   ;; 11
    in    a, (SPI_IN)    ;; 11
    rra                  ;;  4
    rl    d              ;;  8,   total 424

    djnz  loop           ;; 13 * 7 + 8 = 99

    ld   a, d                         ;;  4

    exx                               ;;  4

    ld   (hl), a                      ;;  7   (IX: 19)
    inc  hl                           ;;  6   (IX: 10)

    dec  de                           ;;  6
    ld   a, d                         ;;  4
    or   e                            ;;  4

    exx                               ;;  4

    ret                               ;; 10

                                      ;; 582 T-states


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
    ;; useful subroutine for enc28j60_read_memory above
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