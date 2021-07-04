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
    ;; C   scratch
    ;; DE  outer (byte) loop counter
    ;; HL  destination in RAM
    ;;
    ;; secondary bank (in loop)
    ;; ------------------------
    ;; BC  0 (zero)
    ;; DE  temp register for one term in sum above
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    exx
    ld    hl, (_ip_checksum)

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    ld    c, b  ;; BC := 0 ; B==0 from spi_write_byte

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; -----------------------------------------------------------------------
    ;; Each iteration (16 bits) takes 1341 T-states <=> ~ 42kb/s
    ;; -----------------------------------------------------------------------

word_loop:

    call spi_read_byte_to_memory      ;; 17+630

    ld   e, a                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; the CALL NZ below is not taken,
    ;; and A == B == 0 in the checksum addition instead

    ;; take care not to modify Z flag
    ld   a, c                         ;; 4   A := 0

    call nz, spi_read_byte_to_memory  ;; 17+630

    ld   d, a                         ;; 4

    ex   af, af'                      ;; 4
    adc  hl, de                       ;; 15
    ex   af, af'                      ;; 4

    jr   nz, word_loop                ;; 12

    ;; -----------------------------------------------------------------------
    ;; end of payload: add the final carry to HL
    ;; -----------------------------------------------------------------------

    ex    af, af'
    adc   hl, bc

    ld    (_ip_checksum), hl

    pop   hl                       ;; bring back original HL

do_end_transaction:

    jp   enc28j60_end_transaction_and_return


;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (primary HL), primary C, and A.
;;
;; Primary HL is increased, DE is decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary DE == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    exx                            ;;  4

    call spi_read_byte_to_c        ;; 17 + 7 + 557

    ld   (hl), c                   ;;  7

    dec  de                        ;;  6
    ld   a, d                      ;;  4
    or   e                         ;;  4

    ld   a, c                      ;;  4
    inc  hl                        ;;  6

    exx                            ;;  4

    ret                            ;; 10

                                   ;; 630 T-states


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
    ld   b, #8                 ;;  7

    ;; FALL THROUGH to read_bits_to_c

;; ===========================================================================
;; helper: read_bits_to_c
;;
;; Reads B (typically 8) bits into C. Destroys AF.
;;
;; Reading 8 bits takes 557 T-states.
;; ===========================================================================

read_bits_to_c:

    SPI_READ_BIT_TO   c        ;; 56 * B
    djnz  read_bits_to_c       ;; 13 * (B-1) + 8
    ret                        ;; 10