/*
 * Module enc28j60:
 *
 * Access to the Microchip ENC28J60 Ethernet host.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "enc28j60.h"

#include "util.h"
#include "syslog.h"

/* ========================================================================= */

/* Current checksum */
uint16_t enc28j60_ip_checksum;

/* ------------------------------------------------------------------------- */

void
enc28j60_select_bank(uint8_t bank)
{
  enc28j60_bitfield_clear(ECON1, 0x03);    /* clear bits 0 and 1 in ECON1 */
  enc28j60_bitfield_set(ECON1, bank);
}

/* ------------------------------------------------------------------------- */

void
enc28j60_internal_write8plus8(uint8_t opcode, uint8_t value)
{
  spi_start_transaction(opcode);
  spi_write_byte(value);
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_read_register(uint8_t register_descr)
{
  uint8_t value;
  
  spi_start_transaction(ENC_OPCODE_RCR(register_descr));
  if (IS_MAC_OR_MII(register_descr)) {
    (void) spi_read_byte();             /* dummy byte for MAC/MII registers */
  }
  value = spi_read_byte();
  spi_end_transaction();
  
  return value;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_poll_register(uint8_t register_descr,
                       uint8_t mask,
                       uint8_t value)
{
  int i;
  for (i = 0; i < 10000; i++) {     /* a short while */
    uint8_t r = enc28j60_read_register(register_descr);
    if ((r & mask) == value) {
      return;
    }
  }

  syslog("poll fail");
}

/* ------------------------------------------------------------------------- */

void
enc28j60_read_memory_cont(uint8_t *dst_addr, uint16_t nbr_bytes)
__naked
{
  (void) dst_addr, nbr_bytes;
  
  __asm

    push  ix
    ld    ix, #4     ;; PC + IX on stack
    add   ix, sp

    ;; spi_start_transaction(ENC_OPCODE_RBM);

    ld    a, #ENC_OPCODE_RBM
    push  af
    inc   sp
    call  _spi_write_byte
    inc   sp

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
    ld    hl, (_enc28j60_ip_checksum)
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
word_byte1::
    SPI_READ_BIT_TO_ACC            ;; 448 (56*8)
    djnz word_byte1                ;; 112 (13*8+8)

    exx                            ;; 4
    ld    (de), a                  ;; 7
    inc   de                       ;; 6
    ld    c, a                     ;; 4
    exx                            ;; 4

    ld   b, #8                     ;; 7
word_byte2::
    SPI_READ_BIT_TO_ACC            ;; 448 (56*8)
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
    
word_loop_end::
  
    ;; If there is a single odd byte remaining, handle it

    ld    a, 2(ix)
    rra
    jr    c, odd_byte

    ;; No odd byte, add the remaining C flag

    exx
    ld    bc, #0
    ex    af, af'   ;; '
    jr    final

odd_byte::

    ld   b, #8
odd_byte_loop::
    SPI_READ_BIT_TO_ACC
    djnz odd_byte_loop

    exx
    ld    (de), a
    ld    b, #0
    ld    c, a
    ex    af, af'   ;; '
    adc   hl, bc
    ld    c, #0     ;; BC is now 0

final::
    adc   hl, bc    ;; add final carry
    ld    (_enc28j60_ip_checksum), hl

    SPI_END_TRANSACTION

    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_add_checksum(const void *start_addr, uint16_t nbr_words)
__naked
{
  /* -----------------------------------------------------------------------
   * Can't use the ENC28J60's checksum offload (errata, item #15)
   *
   * Implemented in assembly to save some space.
   * ----------------------------------------------------------------------- */

  (void) start_addr, nbr_words;  /* no warning about unused args */

  __asm

    ld    hl, #2
    add   hl, sp

    ld    e, (hl)
    inc   hl
    ld    d, (hl)
    inc   hl
    ld    c, (hl)
    inc   hl
    ld    b, (hl)   ;;  BC=nbr_words

    push  de
    pop   iy        ;;  IY=start_addr

    ld    hl, (_enc28j60_ip_checksum)

    xor   a         ;; clear addition carry

checksum_loop::
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

checksum_words_done::

    ex    af, af'   ;; ' load addition carry
    adc   hl, bc    ;;  final carry (BC is zero here)

    ld    (_enc28j60_ip_checksum), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory_cont(const uint8_t *src_addr, uint16_t nbr_bytes)
{
  uint16_t i;

  spi_start_transaction(ENC_OPCODE_WBM);
  for (i = 0; i < nbr_bytes; i++) {
    spi_write_byte(*src_addr++);
  }
  spi_end_transaction();
}
