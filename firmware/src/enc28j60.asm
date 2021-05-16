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

#include "spi_asm.h"

    .include "include/enc28j60.inc"
    .include "include/spi.inc"
    .include "include/udp_ip.inc"
    .include "include/util.inc"
    .include "include/ui.inc"

    .area _CODE

;; ############################################################################
;; enc28j60_select_bank_0
;; ############################################################################

enc28j60_select_bank_0:

    ld   e, #0

    ;; FALL THROUGH to enc28j60_select_bank

;; ############################################################################
;; enc28j60_select_bank
;; ############################################################################

enc28j60_select_bank:

    ;; ------------------------------------------------------------------------
    ;; clear bits 0 and 1 of register ECON1
    ;; ------------------------------------------------------------------------

    ld   hl, #0x0100 * 0x03 + OPCODE_BFC + (ECON1 & REG_MASK)
    call enc28j60_write8plus8

    ;; ------------------------------------------------------------------------
    ;; mask in "bank" in bits 0 and 1 of register ECON1
    ;; ------------------------------------------------------------------------

    ld   h, e
    ld   l, #OPCODE_BFS + (ECON1 & REG_MASK)

    ;; fall-through to enc28j60_write8plus8

;; ############################################################################
;; enc28j60_write8plus8
;; ############################################################################

enc28j60_write8plus8:

    ;; ------------------------------------------------------------------------
    ;; start transaction
    ;; ------------------------------------------------------------------------

    ld    c, l
    call  spi_write_byte         ;; preserves HL+DE
    ld    c, h
    call  spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; end transaction
    ;; ------------------------------------------------------------------------

    jr    enc28j60_end_transaction_and_return

;; ############################################################################
;; enc28j60_write_register16
;; ############################################################################

enc28j60_write_register16:

    ld     e, a
    inc    e
    ld     d, h

    ld     h, l
    ld     l, a

    call   enc28j60_write8plus8

    ex     de, hl
    jr     enc28j60_write8plus8

;; ############################################################################
;; enc28j60_write_memory_6_bytes
;; ############################################################################

enc28j60_write_memory_6_bytes:

    ld    de, #6

    ;; FALL THROUGH to enc28j60_write_memory_cont

;; ############################################################################
;; enc28j60_write_memory
;; ############################################################################

enc28j60_write_memory:

    ;; ------------------------------------------------------------------------
    ;; start transaction: WBM
    ;; ------------------------------------------------------------------------

    ld    c, #OPCODE_WBM
    call  spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; write DE bytes, starting at HL
    ;; ------------------------------------------------------------------------

00001$:
    ld    c, (hl)  ;; read byte from data
    inc   hl

    call  spi_write_byte   ;; preserves HL+DE, destroys AF+BC

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
    and   a, #0x1f       ;; opcode RCR = 0x00
    ld    c, a
    call  spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; for MAC and MII registers, read and ignore a dummy byte
    ;; ------------------------------------------------------------------------

    ld    a, e
    add   a, a   ;; bit 7 in descriptor set? then this is a MAC or MII register

    call  c, spi_read_byte

    ;; ------------------------------------------------------------------------
    ;; now read the actual register value
    ;; ------------------------------------------------------------------------

    call  spi_read_byte

    ;; ------------------------------------------------------------------------
    ;; end transaction
    ;; ------------------------------------------------------------------------

enc28j60_end_transaction_and_return:

    ld  a, #SPI_IDLE
    out (SPI_OUT), a
    ld  a, #SPI_IDLE+SPI_CS
    out (SPI_OUT), a

    ret

;; ############################################################################
;; enc28j60_poll_register
;; ############################################################################

enc28j60_poll_register:

    ld     bc, #20000       ;; should give controller plenty of time to respond
00001$:
    push   bc
    call   enc28j60_read_register
    ld     a, c
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

;; ----------------------------------------------------------------------------
;; macro: read one bit from SPI to accumulator
;; assumes C=SPI_IN=SPI_OUT    (on SpeccyBoot; set explicitly for DGBoot below)
;; ----------------------------------------------------------------------------

enc28j60_read_memory:

    push  de         ;; nbr_bytes
    push  hl         ;; dst_addr

    push  ix
    ld    ix, #2     ;; IX on stack
    add   ix, sp

    ;; spi_start_transaction(ENC_OPCODE_RBM);

    ld    c, #OPCODE_RBM
    call  spi_write_byte

    ;;
    ;; assume dst_addr    at (IX + 0)
    ;;        nbr_bytes   at (IX + 2)
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

    ld    d, 1(ix)
    ld    e, 0(ix)
    ld    hl, (_ip_checksum)
    exx

    and   a           ;; reset initial C flag
    ex    af, af'     ;; here's an apostrophe for syntax coloring...

    ld    c, #SPI_OUT
    ld    h, #0x40

    ld    d, 3(ix)
    ld    e, 2(ix)
    rr    d           ;; shift DE right (number of 16-bit words)
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

#ifdef HWTARGET_SPECCYBOOT
    spi_read_bit_to_acc            ;; 448 (56*8)
#endif
#ifdef HWTARGET_DGBOOT
    spi_read_bit_to_acc_dgboot
#endif
    djnz word_byte1                ;; 112 (13*8+8)

    exx                            ;; 4
    ld    (de), a                  ;; 7
    inc   de                       ;; 6
    ld    c, a                     ;; 4
    exx                            ;; 4

    ld   b, #8                     ;; 7
word_byte2:
#ifdef HWTARGET_SPECCYBOOT
    spi_read_bit_to_acc            ;; 448 (56*8)
#endif
#ifdef HWTARGET_DGBOOT
    spi_read_bit_to_acc_dgboot
#endif
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

    ld    a, 2(ix)
    rra
    jr    c, odd_byte

    ;; No odd byte, add the remaining C flag

    exx
    ld    bc, #0
    ex    af, af'   ;; '
    jr    final

odd_byte:

    ld   b, #8
odd_byte_loop:
#ifdef HWTARGET_SPECCYBOOT
    spi_read_bit_to_acc
#endif
#ifdef HWTARGET_DGBOOT
    spi_read_bit_to_acc_dgboot
#endif
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

    pop   ix

    pop   hl
    pop   de

    jp    enc28j60_end_transaction_and_return

;; ############################################################################
;; enc28j60_add_to_checksum
;; ############################################################################

enc28j60_add_to_checksum:

    ;; ------------------------------------------------------------------------
    ;; Can't use the ENC28J60's checksum offload (errata, item #15)
    ;; ------------------------------------------------------------------------

    ld    hl, (_ip_checksum)

    xor   a         ;; clear addition carry

checksum_loop:
    ex    af, af'   ;; ' store addition carry

    ld    a, b
    or    c
    jr    z, checksum_words_done
    dec   bc

    ld    e, (iy)
    inc   iy
    ld    d, (iy)
    inc   iy

    ex    af, af'   ;; ' load addition carry
    adc   hl, de

    jr    checksum_loop

checksum_words_done:

    ex    af, af'   ;; ' load addition carry
    adc   hl, bc    ;;  final carry (BC is zero here)

    ld    (_ip_checksum), hl

    ret
