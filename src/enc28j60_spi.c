/*
 * Module enc28j60_spi:
 *
 * Bit-banged SPI access to the Microchip ENC28J60 Ethernet host. 
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

#include "enc28j60_spi.h"

#include "speccyboot.h"
#include "util.h"
#include "logging.h"

/* ========================================================================= */

/*
 * Registers >= 0x1A are present in all banks, so no switching necessary
 */
#define REGISTER_REQUIRES_BANK_SWITCH(tuple) (REG(tuple) < 0x1A)

/* ============================================================================
 * ENC28J60 SPI HELPERS
 * ========================================================================= */

#define SPI_PORT                    (0x9f)

/* ------------------------------------------------------------------------- */

/*
 * Begin an SPI transaction by pulling SCK & CS low, then transmit opcode.
 */
#define spi_start_transaction(opcode)   spi_write_byte(opcode)

/* ------------------------------------------------------------------------- */

/*
 * End an SPI transaction by pulling SCK low, then CS high.
 */
static void
spi_end_transaction(void)
__naked
{
  __asm
  
  ld  a, #0x40        ; CS=0, RST=1, SCK=0
  out (SPI_PORT), a
  ld  a, #0x48        ; CS=1, RST=1, SCK=0
  out (SPI_PORT), a
  ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Read 8 bits from SPI.
 */
static uint8_t
spi_read_byte(void)
__naked
{
  __asm
  
  ld  bc, #(0x0800 | SPI_PORT)    ; B=8, C=SPI_PORT
  ld  de, #0x4140                 ; D sets CS=1, E sets CS=0

  out (c), e                      ; CS := 0
  
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
  
  ; assumes x to be passed in (sp + 2)
  
  ld  hl, #2
  add hl, sp
  ld  e, (hl)       ; x
  ld  b, #8
  ld  d, #0x80
  
  ; 55 T-states per bit
  
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
  
  {
    uint8_t x[2];
    x[0] = BANK(register_descr);
    x[1] = REG(register_descr);
    logging_add_entry("SPI: poll failed for " HEX8_ARG ":" HEX8_ARG, x);
  }
  
  fatal_error(FATAL_ERROR_SPI_POLL_FAIL);
}

/* ------------------------------------------------------------------------- */

void
enc28j60_read_memory(uint8_t         *dst_addr,
                     enc28j60_addr_t  src_addr,
                     uint16_t         nbr_bytes)
{
  uint16_t i;
  
  enc28j60_write_register(ERDPTH, HIBYTE(src_addr));
  enc28j60_write_register(ERDPTL, LOBYTE(src_addr));
  
  /*
   * TODO: optimize (inline stuff, unroll loop)
   */
  spi_start_transaction(SPI_OPCODE_RBM);
  for (i = 0; i < nbr_bytes; i++) {
    *dst_addr++ = spi_read_byte();
  }
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory_at(enc28j60_addr_t  dst_addr,
                         const uint8_t    *src_addr,
                         uint16_t         nbr_bytes)
{
  enc28j60_write_register(EWRPTH, HIBYTE(dst_addr));
  enc28j60_write_register(EWRPTL, LOBYTE(dst_addr));
  
  enc28j60_write_memory_cont(src_addr, nbr_bytes);
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
