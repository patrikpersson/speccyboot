/*
 * Module enc28j60_spi:
 *
 * Bit-banged SPI access to the Microchip ENC28J60 Ethernet host. Some
 * functionality emulated for EMULATOR_TEST builds.
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "enc28j60_spi.h"

#include "util.h"
#include "syslog.h"

/* ========================================================================= */

/*
 * Registers >= 0x1A are present in all banks, so no switching necessary
 */
#define REGISTER_REQUIRES_BANK_SWITCH(tuple) (REG(tuple) < 0x1A)

/*
 * Macros for beginning and ending an SPI transaction. The braces are there
 * for checking that two matching calls are used.
 */
#define spi_start_transaction(opcode)   spi_write_byte(opcode); {

/*
 * End an SPI transaction by pulling SCK low, then CS high.
 */
#define spi_end_transaction()     }   \
  __asm                               \
  ENC28J60_END_TRANSACTION            \
  __endasm

/* ------------------------------------------------------------------------- */

/*
 * Bank used for emulating the ENC28J60's SRAM during test
 */
#ifdef EMULATOR_TEST
#define EMULATED_ENC28J60_BANK        (1)
#endif

/* ------------------------------------------------------------------------- */

/* ============================================================================
 * ENC28J60 SPI HELPERS (C functions)
 * ========================================================================= */

/*
 * Read 8 bits from SPI.
 */
static uint8_t
spi_read_byte(void)
__naked
{
  __asm
  
  ld  bc, #(0x0800 | SPI_PORT)    ; B=8, C=SPI_PORT
  ld  de, #0x4140                 ; D sets SCK=1, E sets SCK=0

  out (c), e                      ; SCK := 0
  
  ; 58 T-states per bit
  
spi_read_byte_lp:
  in  a, (SPI_PORT)
  rra
  out	(c), d
  rl l
  out (c), e
  djnz spi_read_byte_lp
  
  ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Write 8 bits to SPI.
 */
static void
spi_write_byte(uint8_t x)
__naked
{
  (void) x;       /* silence warning about argument 'x' not used */

  __asm
  
  ;; assumes x to be passed in (sp + 2)
  
  ld  hl, #2
  add hl, sp
  ld  e, (hl)       ; x
  ld  b, #8
  ld  d, #0x80
  
  ;; 55 T-states per bit
  
spi_write_byte_lp:
  ld  a, d          ; a is now 0x80
  rl  e
  rra               ; a is now (0x40 | x.bit << 7)
  out (SPI_PORT), a ; CS=0, SCK=0, MOSI=x.bit
  inc a             ; SCK := 1
  out (SPI_PORT), a
  djnz spi_write_byte_lp
  
  ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Read one bit to accumulator, requires C=0x9f, H=0x40. Destroys F, L.
 * Takes 12 + 12 + 8 + 4 + 4 + 12 + 4
 *   = 56 T-states
 */
#define READ_BIT_TO_ACC   \
  out   (c), h            \
  in    l, (c)            \
  rr    a, l              \
  rla                     \
  inc   h                 \
  out   (c), h            \
  dec   h

/*
 * Read one word to (de), increase de, update checksum in hl'.
 *
 * Requires c=0x9f, h=0x40. Destroys af and l.
 *
 * Takes 16 * 56  +  7 + 6 + 4 + 4 + 4  +  7 + 6 + 4 + 4 + 4 + 15 + 4 + 4
 *   = 969 T-states
 */
#define READ_WORD_TO_DE_INC         \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  ld    (de), a                     \
  inc   de                          \
  exx                               \
  ld    c, a                        \
  exx                               \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  READ_BIT_TO_ACC                   \
  ld    (de), a                     \
  inc   de                          \
  exx                               \
  ld    b, a                        \
  ex    af, af'                     \
  adc   hl, bc                      \
  ex    af, af'                     \
  exx

/*
 * Read a number of bytes from SPI, store them at the indicated location.
 *
 * Returns one-complement 16-bit sum (IP-style checksum), using checksum_in
 * as the initial value.
 */
static uint16_t
spi_read_bytes(uint8_t *dst_addr,
               uint16_t nbr_bytes,
               uint16_t checksum_in)
__naked
{
  (void) dst_addr, nbr_bytes, checksum_in;
  
  __asm

    push  ix
    ld    ix, #4     ;; PC + IX on stack
    add   ix, sp

    ;;
    ;; assume dst_addr    at (IX + 0)
    ;;        nbr_bytes   at (IX + 2)
    ;;        checksum_in at (IX + 4)
    ;;
  
    ;;
    ;; register allocation:
    ;;
    ;; primary bank
    ;; ------------
    ;; B   loop counter, using scheme described below
    ;; C   0x9f     for SPI access
    ;; DE  destination in RAM
    ;; H   0x40     for SPI access
    ;; L   temp register for SPI reads
    ;;
    ;; secondary bank
    ;; --------------
    ;; BC  temp register for one term in sum above
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    ld    h, 5(ix)
    ld    l, 4(ix)
    exx

    and   a           ;; reset initial C flag
    ex    af, af'     ;; here's an apostrophe for syntax coloring...

    ld    c, #0x9f
    ld    d, 1(ix)
    ld    e, 0(ix)
    ld    h, #0x40
  
    ld    a, 3(ix)
    ld    l, 2(ix)
    rra
    rr    l
    rra
    rr    l
    rra
    rr    l
    xor   a
    sub   l
    jp    z, unroll_end
    
    ld    b, a            ;; B is now (0x0100 - (N >> 3))
    
    ;;
    ;; B is increased until zero. The reason for this funny counting is to
    ;; stay away from contended I/O addresses.
    ;;
    ;; http://www.worldofspectrum.org/faq/reference/48kreference.htm
    ;;

    ;;
    ;; each iteration takes 4 * 969 + 4 + 10
    ;;   =  3890 T-states
    ;;   =  (3890 / 3.5e6) seconds
    ;;   =  1.11ms
    ;;  <=> 57583 bits/second
    ;;

unrolled_loop:
    READ_WORD_TO_DE_INC
    READ_WORD_TO_DE_INC
    READ_WORD_TO_DE_INC
    READ_WORD_TO_DE_INC
  
    inc   b
    jp    nz, unrolled_loop
    
unroll_end::

    /*
     * Handle the remaining full 16-bit words (0, 1, 2 or 3)
     */
    ld    a, 2(ix)
    rra
    and   a, #0x03
    jp    z, no_words
    ld    b, a
    xor   a
    sub   b
    ld    b, a

loose_words::
    READ_WORD_TO_DE_INC
    inc   b
    jp    nz, loose_words

no_words::
  
    /*
     * If there is a single odd byte remaining, handle it
     */
    ld    a, 2(ix)
    rra
    jr    c, odd_byte

    /*
     * No odd byte, add the remaining C flag
     */
    exx
    ld    bc, #0
    ex    af, af'   ;; '
    adc   hl, bc
    pop   ix
    ret

odd_byte::
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    READ_BIT_TO_ACC
    ld    (de), a
    exx
    ld    b, #0
    ld    c, a
    ex    af, af'   ;; '
    adc   hl, bc
    ld    c, #0     ;; BC is now 0
    adc   hl, bc    ;; add final carry
    pop   ix
    ret

  __endasm;
}

/* ========================================================================= */

void
enc28j60_init(void)
{
  __asm

  ;; Reset controller the hardware way (by pulling RST low)
  ;;
  ;; Data sheet, Table 16.3: Trstlow = 400ns
  ;; (minimal RST low time, shorter pulses are filtered out)
  ;;
  ;; 400ns < 2 T-states == 571ns    (no problem at all)
  
  xor a
  out (SPI_PORT), a   ;; Assert RST
  ld  a, #0x40        ;;
  out (SPI_PORT), a   ;; Release RST
  
  ;; Data sheet, #11.2:
  ;;
  ;; Wait at least 50us after a System Reset before accessing PHY registers.
  ;; Perform an explicit delay here to be absolutely sure.
  ;;
  ;; 14 iterations, each is 13 T-states, 14x13 = 182 T-states > 51us @3.55MHz
  
  ld b, #14
enc28j60_init_loop:
  djnz  enc28j60_init_loop
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_select_bank(uint16_t register_descr)
{
  static uint8_t   current_bank = 0xff;   /* force switch on first call */
         uint8_t requested_bank = BANK(register_descr);
  
  if (REGISTER_REQUIRES_BANK_SWITCH(register_descr)
      && requested_bank != current_bank)
  {
    enc28j60_bitfield_clear(ECON1, 0x03);    /* clear bits 0 and 1 in ECON1 */
    enc28j60_bitfield_set(ECON1, requested_bank);
    current_bank = requested_bank;
  }
}

/* ------------------------------------------------------------------------- */

void
enc28j60_internal_write8plus8(uint8_t opcode,
                              uint16_t register_descr,
                              uint8_t value)
{
  enc28j60_select_bank(register_descr);
  
  spi_start_transaction(opcode);
  spi_write_byte(value);
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_register16(uint16_t register_descr, uint16_t new_value)
{
  enc28j60_write_register(register_descr, LOBYTE(new_value));
  enc28j60_write_register(register_descr + 1,  /* next register in same bank */
                          HIBYTE(new_value));
}

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_read_register(uint16_t register_descr)
{
  uint8_t value;
  
  enc28j60_select_bank(register_descr);
  
  spi_start_transaction(SPI_OPCODE_RCR(register_descr));
  if (IS_MAC_OR_MII(register_descr)) {
    (void) spi_read_byte();             /* dummy byte for MAC/MII registers */
  }
  value = spi_read_byte();
  spi_end_transaction();
  
  return value;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_poll_register(uint16_t register_descr,
                       uint8_t mask,
                       uint8_t value)
{
  int i;
  for (i = 0; i < 10000; i++) {     /* a short while */
    uint8_t r = enc28j60_read_register(register_descr);
    if ((r & mask) == value) return;
  }

  syslog("SPI time-out, reg %, mask %, val %",
         register_descr, (int) mask, (int) value);
}

/* ------------------------------------------------------------------------- */

uint16_t
enc28j60_read_memory_cont(uint8_t  *dst_addr,
                          uint16_t  nbr_bytes,
                          uint16_t  checksum_in)
{
  uint16_t checksum_out;

  spi_start_transaction(SPI_OPCODE_RBM);

  checksum_out = spi_read_bytes(dst_addr, nbr_bytes, checksum_in);
  
  spi_end_transaction();
  
  return checksum_out;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory_at(enc28j60_addr_t  dst_addr,
                         const uint8_t    *src_addr,
                         uint16_t         nbr_bytes)
{
#ifdef EMULATOR_TEST
  
  select_bank(EMULATED_ENC28J60_BANK);
  
  memcpy(ENC28J60_EMULATED_SRAM_ADDR + dst_addr, src_addr, nbr_bytes);
  
  select_bank(DEFAULT_BANK);
  
#else /* EMULATOR_TEST */  

  enc28j60_write_register(EWRPTH, HIBYTE(dst_addr));
  enc28j60_write_register(EWRPTL, LOBYTE(dst_addr));
  
  enc28j60_write_memory_cont(src_addr, nbr_bytes);
  
#endif
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory_cont(const uint8_t   *src_addr,
                           uint16_t         nbr_bytes)
{
  uint16_t i;
  
  spi_start_transaction(SPI_OPCODE_WBM);
  for (i = 0; i < nbr_bytes; i++) {
    spi_write_byte(*src_addr++);
  }
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_nwu16_cont(uint16_t nw_order_value)
{
  /*
   * Assume little-endian host (Z80)
   */
  enc28j60_write_memory_cont(((const uint8_t *) &nw_order_value) + 1, 1);
  enc28j60_write_memory_cont((const uint8_t *) &nw_order_value, 1);
}

/* ------------------------------------------------------------------------- */

void
enc28j60_clear_memory_at(enc28j60_addr_t  dst_addr,
                         uint16_t         nbr_bytes)
{
  uint16_t i;
  
  enc28j60_write_register(EWRPTH, HIBYTE(dst_addr));
  enc28j60_write_register(EWRPTL, LOBYTE(dst_addr));
  
  spi_start_transaction(SPI_OPCODE_WBM);
  for (i = 0; i < nbr_bytes; i++) {
    spi_write_byte(0);
  }
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_load_byte_at_address(void)
__naked
{
  __asm

#ifdef EMULATOR_TEST
  
  ;; store BC somewhere good (will distort picture in top right)
  
  ld    (0x401e), bc
  
  ;; switch to ENC28J60 emulated SRAM, read byte, switch back to default bank
  
  ld    a, #EMULATED_ENC28J60_BANK
  ld    bc, #0x7ffd
  out   (c), a
  
  ld    b, #0xD7          ;; HIBYTE(saved_app_data)
  ld    a, (0x401e)
  ld    c, a
  ld    a, (bc)
  ld    (0x401d), a
  
  ld    bc, #0x7ffd
  ld    a, #DEFAULT_BANK
  out   (c), a
  
  ld    bc, (0x401e)
  ld    a, (0x401d)
  ld    c, a
  ld    a, r      ;; ensure we do not somehow depend on value of A

#else /* EMULATOR_TEST */
  
  ;;
  ;; write constants 0x41 0x17 (ERDPTH := 0x17)
  ;;
  
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_1
  
  ENC28J60_END_TRANSACTION
  
  ;;
  ;; write constant 0x40 followed by C register (ERDPTL := C)
  ;;

  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0

  ENC28J60_WRITE_FROM(c)

  ENC28J60_END_TRANSACTION

  ;;
  ;; write constant 0x3A, then read one byte into C register (C := *ERDPTL)
  ;;

  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_0
  ENC28J60_WRITE_BIT_1
  ENC28J60_WRITE_BIT_0

  ENC28J60_READ_TO(c)
  
  ENC28J60_END_TRANSACTION
  
#endif /* EMULATOR_TEST */
  
  jp    (hl)

  __endasm;
}
