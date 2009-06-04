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

#include "speccyboot.h"
#include "util.h"
#include "logging.h"

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
  for (i = 0; i < 30000; i++) {     /* a few seconds */
    uint8_t r = enc28j60_read_register(register_descr);
    if ((r & mask) == value) return;
  }
  
  fatal_error(FATAL_ERROR_SPI_POLL_FAIL);
}

/* ------------------------------------------------------------------------- */

/*
 * Read one bit to L, requires c=0x9f, h=0x40
 */
#define READ_BIT_TO_L         \
  out   (c), h                \
  in    a, (c)                \
  rra                         \
  rl    a, l                  \
  inc   h                     \
  out   (c), h                \
  dec   h

void
enc28j60_read_memory(uint8_t         *dst_addr,
                     enc28j60_addr_t  src_addr,
                     uint16_t         nbr_bytes)
{
  enc28j60_write_register(ERDPTH, HIBYTE(src_addr));
  enc28j60_write_register(ERDPTL, LOBYTE(src_addr));

  spi_start_transaction(SPI_OPCODE_RBM);

  if (nbr_bytes == 512) {
    /*
     * Optimized case for reading an entire TFTP file block
     */
    __asm
    
    ;;
    ;; assume dst_addr at (IX + 4)
    ;;
    
    ld    e, 4(ix)
    ld    d, 5(ix)
    ld    bc, #0x809f   
    ld    h, #0x40
    
    ;;
    ;; B is set to 0x80, and increased until zero, so we get 128 iterations.
    ;; The reason for this funny counting is to stay away from contended I/O
    ;; addresses.
    ;;
    ;; http://www.worldofspectrum.org/faq/reference/48kreference.htm
    ;;
    
99999$:
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L

    ld    a, l
    ld    (de), a
    inc   de
      
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    
    ld    a, l
    ld    (de), a
    inc   de
      
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    
    ld    a, l
    ld    (de), a
    inc   de
    
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    READ_BIT_TO_L
    
    ld    a, l
    ld    (de), a
    inc   de
    
    inc   b
    jr    z, 99998$
    jp    99999$
    
99998$:
    __endasm;
  }  
  else {
    while (nbr_bytes --) {
      *dst_addr++ = spi_read_byte();
    }
  }
  
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory_at(enc28j60_addr_t  dst_addr,
                         const uint8_t    *src_addr,
                         uint16_t         nbr_bytes)
{
#ifdef EMULATOR_TEST
  
  select_bank(0);
  
  memcpy(ENC28J60_EMULATED_SRAM_ADDR + dst_addr, src_addr, nbr_bytes);
  
  select_bank(1);
  
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
enc28j60_load_byte_at_address(void)
__naked
{
  __asm

#ifdef EMULATOR_TEST
  
  ;; store BC somewhere good (will distort picture in top right)
  
  ld    (0x401e), bc
  
  ;; switch to bank 0, read byte, switch back to bank 1
  
  xor   a
  ld    bc, #0x7ffd
  out   (c), a
  
  ld    b, #0xD7          ;; HIBYTE(saved_app_data)
  ld    a, (0x401e)
  ld    c, a
  ld    a, (bc)
  ld    (0x401d), a
  
  ld    bc, #0x7ffd
  ld    a, #1
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