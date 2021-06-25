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
    ;; primary bank
    ;; ------------
    ;; B   inner (bit) loop counter, always in range 0..8
    ;; C   byte read from SPI
    ;; DE  outer (byte) loop counter
    ;; HL  destination in RAM
    ;;
    ;; secondary bank
    ;; --------------
    ;; BC  temp register for one term in sum above
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

word_loop:

    call read_byte                 ;; 17 + 7 + 448 + 112 + 10

    ld   c, a                      ;; 4
    jr   z, add_zero_padding_byte  ;; 10

    call read_byte                 ;; 17 + 7 + 448 + 112 + 10

    ld    b, a                     ;; 4
    ex    af, af'                  ;; 4
    adc   hl, bc                   ;; 15
    ex    af, af'                  ;; 4

    jr   nz, word_loop             ;; 12

    ex    af, af'
    jr    final

add_zero_padding_byte:

;; ----------------------------------------------------------------------------
;; these two instructions happen to be 0x08, 0x06, which is the ARP ethertype
;; (used in eth.c)
;; ----------------------------------------------------------------------------
ethertype_arp:
    ex    af, af'
    ld    b, #0                    ;; padding byte for checksum

    adc   hl, bc

final:

    ;; If we didn't ensure B == 0 on return, this could be done as
    ;; JR NC, ... ; INC HL  saving one byte

    ld    bc, #0
    adc   hl, bc

    ld    (_ip_checksum), hl

    pop   hl                       ;; bring back original HL

    jr    do_end_transaction

;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (HL), A, and C.
;;
;; Primary HL is increased, B :=0, DE is decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary DE == 0.
;; ----------------------------------------------------------------------------

read_byte:

    exx                            ;; 4    to primary

    ld   b, #8
read_byte_loop:
    spi_read_bit_to_c
    djnz read_byte_loop

    dec  de                        ;; 6
    ld   a, d                      ;; 4
    or   e                         ;; 4

    ld   (hl), c
    inc  hl

    ld   a, c

    exx

    ret


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
