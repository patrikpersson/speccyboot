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
    .optsdcc -mz80

    .include "enc28j60.inc"

    .include "spi.inc"
    .include "udp_ip.inc"
    .include "util.inc"

    .area _CODE

;; ############################################################################
;; enc28j60_poll_register
;; ############################################################################

enc28j60_poll_register:

    ld     bc, #20000       ;; should give controller plenty of time to respond
00001$:
    push   bc
    call   enc28j60_read_register
    pop    bc

    and    a, h
    cp     a, l
    ret    z

    dec    bc
    ld     a, b
    or     a, c
    jr     nz, 00001$

    ld     a, #FATAL_INTERNAL_ERROR
    jp     fail

;; ############################################################################
;; enc28j60_read_memory
;; ############################################################################

enc28j60_read_memory:

    push  hl         ;; dst_addr

    push  de         ;; number of bytes

    ;; spi_start_transaction(ENC_OPCODE_RBM);

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    ;;
    ;; assume nbr_bytes at (IX + 0)
    ;;

    ;;
    ;; register allocation:
    ;;
    ;; primary bank
    ;; ------------
    ;; B   inner (bit) loop counter, always in range 0..8
    ;; C   0x9f     for SPI access
    ;; DE  outer (word) loop counter
    ;; H   0x40     for SPI access
    ;; L   temp register for SPI reads
    ;;
    ;; secondary bank
    ;; --------------
    ;; BC  temp register for one term in sum above
    ;; DE  destination in RAM
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    ex    de, hl    ;; DE now holds destination address
    ld    hl, (_ip_checksum)
    exx

    and   a, a        ;; reset initial C flag
    ex    af, af'     ;; here's an apostrophe for syntax coloring...

    ld    c, #SPI_OUT
    ld    h, #0x40

    pop   de
    push  de
    srl   d           ;; shift DE right (number of 16-bit words)
    rr    e

    ;; Read one word to (de'), increase de', update checksum in hl'.   '
    ;;
    ;; Each iteration takes   4+4+10+6
    ;;                       +7+448+112+4+7+6+4+4+7
    ;;                       +7+448+112+4+7+6+4+4+15+4+4
    ;;                       +12
    ;;                     = 24 + 599 + 615 + 12
    ;;                     = 1250 T-states
    ;;                     = 3.57143ms @3.5MHz
    ;;                    <=> 44800 bits/second

word_loop:
    ld   a, d                      ;; 4
    or   e                         ;; 4
    jp   z, word_loop_end          ;; 10
    dec  de                        ;; 6

    ld   b, #8                     ;; 7
word_byte1:

    spi_read_bit_to_acc            ;; 448 (56*8)

    djnz word_byte1                ;; 112 (13*8+8)

    exx                            ;; 4
    ld    (de), a                  ;; 7
    inc   de                       ;; 6
    ld    c, a                     ;; 4
    exx                            ;; 4

    ld   b, #8                     ;; 7
word_byte2:

    spi_read_bit_to_acc            ;; 448 (56*8)

    djnz word_byte2                ;; 112 (13*8+8)

    exx                            ;; 4
    ld    (de), a                  ;; 7
    inc   de                       ;; 6
    ld    b, a                     ;; 4
    ex    af, af'                  ;; 4'
    adc   hl, bc                   ;; 15
    ex    af, af'                  ;; 4'
    exx                            ;; 4

    jr    word_loop                ;; 12

word_loop_end:

    ;; If there is a single odd byte remaining, handle it

    pop   af    ;; number of bytes, lowest bit now in C flag
    jr    c, odd_byte

    ;; No odd byte, add the remaining C flag

    exx
    ld    bc, #0
    ex    af, af'   ;; '
    jr    final

odd_byte:

    ld   b, #8
odd_byte_loop:

    spi_read_bit_to_acc

    djnz odd_byte_loop

    exx
    ld    (de), a
    ld    c, a

;; ----------------------------------------------------------------------------
;; these two instructions happen to be 0x08, 0x06, which is the ARP ethertype
;; (used in eth.c)
;; ----------------------------------------------------------------------------
ethertype_arp:
    ex    af, af'   ;; '
    ld    b, #0

    adc   hl, bc

;; ----------------------------------------------------------------------------
;; this instruction happens to be 0x0E, which is the ENC28J60H per-packet
;; control byte (datasheet, section 7.1)
;; ----------------------------------------------------------------------------
eth_control_byte:
    ld    c, #0     ;; BC is now 0

final:
    adc   hl, bc    ;; add final carry
    ld    (_ip_checksum), hl

    pop   hl

    jr    enc28j60_end_transaction_and_return

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
;; enc28j60_write_memory
;; ############################################################################

enc28j60_write_memory:

    ;; ------------------------------------------------------------------------
    ;; start transaction: WBM
    ;; ------------------------------------------------------------------------

    ld    c, #OPCODE_WBM
    rst   spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; write DE bytes, starting at HL
    ;; ------------------------------------------------------------------------

00001$:
    ld    c, (hl)  ;; read byte from data
    inc   hl

    rst   spi_write_byte   ;; preserves HL+DE, destroys AF+BC

    dec   de
    ld    a, d
    or    a, e
    jr    nz, 00001$

    ;; ------------------------------------------------------------------------
    ;; end transaction
    ;; ------------------------------------------------------------------------

    jr    enc28j60_end_transaction_and_return

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

    ;; ------------------------------------------------------------------------
    ;; end transaction
    ;; ------------------------------------------------------------------------

enc28j60_end_transaction_and_return:

    ld  a, #SPI_IDLE
    out (SPI_OUT), a
    ld  a, #SPI_IDLE+SPI_CS
    out (SPI_OUT), a

    ld  a, c

    ret
