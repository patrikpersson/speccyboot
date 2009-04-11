/*
 * Module enc28j60:
 *
 * Basic access to control registers and on-chip memory of the
 * Microchip ENC28J60 Ethernet host.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "enc28j60.h"
#include "spectrum.h"
#include "speccyboot.h"
#include "logging.h"

/* ========================================================================= */

/*
 * The ENC28J60 SRAM is used as follows:
 *
 * 0x0000 ... 0x17FF    6K  Receive buffer
 * 0x1800 ... 0x1BFF    1K  Transmit buffer
 * 0x1C00 ... 0x1FFF    1K  Unused
 *
 * Errata for silicon rev. 5 suggests receive buffer in low memory (item 3)
 */

#define ENC28J60_RXBUF_START    (0x0000)
#define ENC28J60_RXBUF_END      (0x17FF)
#define ENC28J60_TXBUF_START    (0x1800)
#define ENC28J60_TXBUF_END      (0x1BFF)

/* TFTP packets have up to 512 bytes payload */
#define ENC28J60_FRAME_MAX      (640)

/* ========================================================================= */

/* ------------------------------------------------------------------------- */

#define ECON1_RXEN    (4)

/* ------------------------------------------------------------------------- */

/*
 * Useful macro for avoiding warnings about unused arguments
 */
#define ARG_NOT_USED(a)  ((void) a)

/* ------------------------------------------------------------------------- */

/*
 * Opcodes for SPI commands
 */
#define SPI_OPCODE_RCR(reg)     (0x00 | (reg))
#define SPI_OPCODE_WCR(reg)     (0x40 | (reg))
#define SPI_OPCODE_RBM          (0x3A)
#define SPI_OPCODE_WBM          (0x7A)

/* ------------------------------------------------------------------------- */

/*
 * Bit masks for SPI accesses
 */
#define SPI(clk, mosi)          (SPI_RST                             \
                                 | ((SPI_SCK) * (clk))               \
                                 | ((SPI_MOSI) * (mosi)))

/* ------------------------------------------------------------------------- */

static void
SPI_DELAY(void)
{
  /*
   * Delay loop (> 1ms @3.55MHz)
   */
  __asm
  ld  b, #0
lp:
  nop
  djnz  lp
  __endasm;
}

/* ------------------------------------------------------------------------- */

#define SPI_PORT      (0x9f)

/* ========================================================================= */

static void
spi_start_transaction(void)
__naked
{
  __asm
  
  ld  a, #0x40        ; CS=0, RST=1, SCK=0
  out (SPI_PORT), a
  ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

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

#pragma save
#pragma disable_warning 85
/* silence warning about argument 'x' not used */

static void
spi_write_byte(uint8_t x)
__naked
{
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

#pragma restore
  
/* ========================================================================= */

#pragma save
#pragma disable_warning 116
/* warning since HIBYTE(ENC28J60_TXBUF_START) is zero */

void
enc28j60_init(struct mac_address_t *mac_address)
{
  Z80_PORT_WRITE(sbt_cfg_port, 0);            /* SPI_RST -> 0 */
  SPI_DELAY();
  Z80_PORT_WRITE(sbt_cfg_port, SPI_RST);      /* SPI_RST -> 1 */
  SPI_DELAY();

  /*
   * ETH initialization
   */
  enc28j60_write_register(ETXSTL, LOBYTE(ENC28J60_TXBUF_START));
  enc28j60_write_register(ETXSTH, HIBYTE(ENC28J60_TXBUF_START));
  enc28j60_write_register(ETXNDL, LOBYTE(ENC28J60_TXBUF_END));
  enc28j60_write_register(ETXNDH, HIBYTE(ENC28J60_TXBUF_END));
  
  enc28j60_write_register(ERXSTL, LOBYTE(ENC28J60_RXBUF_START));
  enc28j60_write_register(ERXSTH, HIBYTE(ENC28J60_RXBUF_START));
  enc28j60_write_register(ERXNDL, LOBYTE(ENC28J60_RXBUF_END));
  enc28j60_write_register(ERXNDH, HIBYTE(ENC28J60_RXBUF_END));

  enc28j60_write_register(ERXRDPTL, LOBYTE(ENC28J60_RXBUF_START));
  enc28j60_write_register(ERXRDPTH, HIBYTE(ENC28J60_RXBUF_START));

  enc28j60_write_register(ERXFCON, 0x00);    /* TODO: enable CRC check */
  enc28j60_write_register(ECON2, 0x80);      /* AUTOINC=1 */

  /* TODO: poll ESTAT.CLKRDY? (datasheet, 6.4) */

  /*
   * MAC initialization
   */
  enc28j60_write_register(MACON1, 0x01);  /* TXPAUS=1 */
  enc28j60_write_register(MACON3, 0xF3);  /* padding, auto CRC, full dpx, check */
  enc28j60_write_register(MACON4, 0x00);  /* TODO: bit 6 for infinite wait? */

  enc28j60_write_register(MAMXFLL, LOBYTE(ENC28J60_FRAME_MAX));
  enc28j60_write_register(MAMXFLH, HIBYTE(ENC28J60_FRAME_MAX));
  
  enc28j60_write_register(MABBIPG, 0x15);
  enc28j60_write_register(MAIPGL, 0x12);
  
  enc28j60_write_register(MAADR1, mac_address->addr[0]);
  enc28j60_write_register(MAADR2, mac_address->addr[1]);
  enc28j60_write_register(MAADR3, mac_address->addr[2]);
  enc28j60_write_register(MAADR4, mac_address->addr[3]);
  enc28j60_write_register(MAADR5, mac_address->addr[4]);
  enc28j60_write_register(MAADR6, mac_address->addr[5]);

  /*
   * PHY initialization
   */
  enc28j60_write_phy_register(PHCON1, 0x0100);   /* full duplex */
}

#pragma restore

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_read_register(bool    is_mac_or_mii,
                       uint8_t bank,
                       uint8_t reg)
{
  uint8_t value;
  
  if (reg < REGISTERS_IN_ALL_BANKS) {
    enc28j60_write_register(ECON1, bank | ECON1_RXEN);  /* select bank */
  }
  
  spi_start_transaction();
  spi_write_byte(SPI_OPCODE_RCR(reg));
  if (is_mac_or_mii) {
    (void) spi_read_byte();     /* dummy byte */
  }
  value = spi_read_byte();
  spi_end_transaction();
  
  return value;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_register(bool    is_mac_or_mii,
                        uint8_t bank,
                        uint8_t reg,
                        uint8_t value)
{
  ARG_NOT_USED(is_mac_or_mii);
  
  if (reg < REGISTERS_IN_ALL_BANKS) {
    enc28j60_write_register(ECON1, bank | ECON1_RXEN);  /* select bank */
  }
  
  spi_start_transaction();
  spi_write_byte(SPI_OPCODE_WCR(reg));
  spi_write_byte(value);
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_phy_register(enum enc28j60_phy_reg_t  reg,
                            uint16_t                 value)
{
  /* datasheet, section 3.3.2 */
  enc28j60_write_register(MIREGADR, (uint8_t) reg);
  enc28j60_write_register(MIWRL, LOBYTE(value));
  enc28j60_write_register(MIWRH, HIBYTE(value));
  /* delay >= 10.24us == 36 T-states */
  __asm
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_read_memory(uint8_t *        dst_addr,
                     enc28j60_addr_t  src_addr,
                     uint16_t         nbr_bytes)
{
  uint16_t i;
  
  enc28j60_write_register(ERDPTH, HIBYTE(src_addr));
  enc28j60_write_register(ERDPTL, LOBYTE(src_addr));

  /*
   * TODO: optimize (inline stuff, unroll loop)
   */
  spi_start_transaction();
  spi_write_byte(SPI_OPCODE_RBM);
  for (i = 0; i < nbr_bytes; i++) {
    *dst_addr++ = spi_read_byte();
  }
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_memory(enc28j60_addr_t  dst_addr,
                      uint8_t *        src_addr,
                      uint16_t         nbr_bytes)
{
  uint16_t i;
  
  enc28j60_write_register(EWRPTH, HIBYTE(dst_addr));
  enc28j60_write_register(EWRPTL, LOBYTE(dst_addr));
  
  spi_start_transaction();
  spi_write_byte(SPI_OPCODE_WBM);
  for (i = 0; i < nbr_bytes; i++) {
    spi_write_byte(*src_addr++);
  }
  spi_end_transaction();
}

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_poll(void)
{
  return (Z80_PORT_READ(sbt_cfg_port)
          & (ENC28J60_INT_ACTIVE | ENC28J60_WOL_ACTIVE))
         ^ (ENC28J60_INT_ACTIVE | ENC28J60_WOL_ACTIVE);
}
