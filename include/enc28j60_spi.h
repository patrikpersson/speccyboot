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

#ifndef SPECCYBOOT_ENC28J60_SPI_INCLUSION_GUARD
#define SPECCYBOOT_ENC28J60_SPI_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * ENC28J60 ETH/MAC/MII control registers
 * ------------------------------------------------------------------------- */

/*
 * Encode register info into a single value
 */
#define REGISTER_TUPLE(is_mac_or_mii, bank, reg)                              \
   (((bank) * 0x0100) | ((is_mac_or_mii) ? 0x80 : 0x00) | (reg))

#define IS_MAC_OR_MII(tuple)    (LOBYTE(tuple) & 0x80)
#define BANK(tuple)             HIBYTE(tuple)
#define REG(tuple)              (LOBYTE(tuple) & 0x1f)

#define ERDPTL                  REGISTER_TUPLE(false, 0, 0x00)
#define ERDPTH                  REGISTER_TUPLE(false, 0, 0x01)
#define EWRPTL                  REGISTER_TUPLE(false, 0, 0x02)
#define EWRPTH                  REGISTER_TUPLE(false, 0, 0x03)
#define ETXSTL                  REGISTER_TUPLE(false, 0, 0x04)
#define ETXSTH                  REGISTER_TUPLE(false, 0, 0x05)
#define ETXNDL                  REGISTER_TUPLE(false, 0, 0x06)
#define ETXNDH                  REGISTER_TUPLE(false, 0, 0x07)
#define ERXSTL                  REGISTER_TUPLE(false, 0, 0x08)
#define ERXSTH                  REGISTER_TUPLE(false, 0, 0x09)
#define ERXNDL                  REGISTER_TUPLE(false, 0, 0x0a)
#define ERXNDH                  REGISTER_TUPLE(false, 0, 0x0b)
#define ERXRDPTL                REGISTER_TUPLE(false, 0, 0x0c)
#define ERXRDPTH                REGISTER_TUPLE(false, 0, 0x0d)
#define ERXWRPTL                REGISTER_TUPLE(false, 0, 0x0e)
#define ERXWRPTH                REGISTER_TUPLE(false, 0, 0x0f)
#define EDMASTL                 REGISTER_TUPLE(false, 0, 0x10)
#define EDMASTH                 REGISTER_TUPLE(false, 0, 0x11)
#define EDMANDL                 REGISTER_TUPLE(false, 0, 0x12)
#define EDMANDH                 REGISTER_TUPLE(false, 0, 0x13)
#define EDMADSTL                REGISTER_TUPLE(false, 0, 0x14)
#define EDMADSTH                REGISTER_TUPLE(false, 0, 0x15)
#define EDMACSL                 REGISTER_TUPLE(false, 0, 0x16)
#define EDMACSH                 REGISTER_TUPLE(false, 0, 0x17)
#define EIE                     REGISTER_TUPLE(false, 0, 0x1b)
#define EIR                     REGISTER_TUPLE(false, 0, 0x1c)
#define ESTAT                   REGISTER_TUPLE(false, 0, 0x1d)
#define ECON2                   REGISTER_TUPLE(false, 0, 0x1e)
#define ECON1                   REGISTER_TUPLE(false, 0, 0x1f)

#define EHT0                    REGISTER_TUPLE(false, 1, 0x00)
#define EHT1                    REGISTER_TUPLE(false, 1, 0x01)
#define EHT2                    REGISTER_TUPLE(false, 1, 0x02)
#define EHT3                    REGISTER_TUPLE(false, 1, 0x03)
#define EHT4                    REGISTER_TUPLE(false, 1, 0x04)
#define EHT5                    REGISTER_TUPLE(false, 1, 0x05)
#define EHT6                    REGISTER_TUPLE(false, 1, 0x06)
#define EHT7                    REGISTER_TUPLE(false, 1, 0x07)
#define EPMM0                   REGISTER_TUPLE(false, 1, 0x08)
#define EPMM1                   REGISTER_TUPLE(false, 1, 0x09)
#define EPMM2                   REGISTER_TUPLE(false, 1, 0x0a)
#define EPMM3                   REGISTER_TUPLE(false, 1, 0x0b)
#define EPMM4                   REGISTER_TUPLE(false, 1, 0x0c)
#define EPMM5                   REGISTER_TUPLE(false, 1, 0x0d)
#define EPMM6                   REGISTER_TUPLE(false, 1, 0x0e)
#define EPMM7                   REGISTER_TUPLE(false, 1, 0x0f)
#define EPMCSL                  REGISTER_TUPLE(false, 1, 0x10)
#define EPMCSH                  REGISTER_TUPLE(false, 1, 0x11)
#define EPMOL                   REGISTER_TUPLE(false, 1, 0x14)
#define EPMOH                   REGISTER_TUPLE(false, 1, 0x15)
#define EWOLIE                  REGISTER_TUPLE(false, 1, 0x16)
#define EWOLIR                  REGISTER_TUPLE(false, 1, 0x17)
#define ERXFCON                 REGISTER_TUPLE(false, 1, 0x18)
#define EPKTCNT                 REGISTER_TUPLE(false, 1, 0x19)

#define MACON1                  REGISTER_TUPLE(true,  2, 0x00)
#define MACON3                  REGISTER_TUPLE(true,  2, 0x02)
#define MACON4                  REGISTER_TUPLE(true,  2, 0x03)
#define MABBIPG                 REGISTER_TUPLE(true,  2, 0x04)
#define MAIPGL                  REGISTER_TUPLE(true,  2, 0x06)
#define MAIPGH                  REGISTER_TUPLE(true,  2, 0x07)
#define MACLCON1                REGISTER_TUPLE(true,  2, 0x08)
#define MACLCON2                REGISTER_TUPLE(true,  2, 0x09)
#define MAMXFLL                 REGISTER_TUPLE(true,  2, 0x0a)
#define MAMXFLH                 REGISTER_TUPLE(true,  2, 0x0b)
#define MICMD                   REGISTER_TUPLE(true,  2, 0x12)
#define MIREGADR                REGISTER_TUPLE(true,  2, 0x14)
#define MIWRL                   REGISTER_TUPLE(true,  2, 0x16)
#define MIWRH                   REGISTER_TUPLE(true,  2, 0x17)
#define MIRDL                   REGISTER_TUPLE(true,  2, 0x18)
#define MIRDH                   REGISTER_TUPLE(true,  2, 0x19)

#define MAADR5                  REGISTER_TUPLE(true,  3, 0x00)
#define MAADR6                  REGISTER_TUPLE(true,  3, 0x01)
#define MAADR3                  REGISTER_TUPLE(true,  3, 0x02)
#define MAADR4                  REGISTER_TUPLE(true,  3, 0x03)
#define MAADR1                  REGISTER_TUPLE(true,  3, 0x04)
#define MAADR2                  REGISTER_TUPLE(true,  3, 0x05)
#define EBSTSD                  REGISTER_TUPLE(false, 3, 0x06)
#define EBSTCON                 REGISTER_TUPLE(false, 3, 0x07)
#define EBSTCSL                 REGISTER_TUPLE(false, 3, 0x08)
#define EBSTCSH                 REGISTER_TUPLE(false, 3, 0x09)
#define MISTAT                  REGISTER_TUPLE(true,  3, 0x0a)
#define EREVID                  REGISTER_TUPLE(false, 3, 0x12)
#define ECOCON                  REGISTER_TUPLE(false, 3, 0x15)
#define EFLOCON                 REGISTER_TUPLE(false, 3, 0x17)
#define EPAUSL                  REGISTER_TUPLE(false, 3, 0x18)
#define EPAUSH                  REGISTER_TUPLE(false, 3, 0x19)

/* -------------------------------------------------------------------------
 * ENC28J60 PHY control registers  (datasheet table 3-3)
 * ------------------------------------------------------------------------- */

#define PHCON1                  (0x00)
#define PHSTAT2                 (0x11)

/* -------------------------------------------------------------------------
 * Individual bits in ETH, MAC, MII registers
 * ------------------------------------------------------------------------- */

#define ESTAT_CLKRDY            (0x01)
#define ESTAT_TXABRT            (0x02)
#define ECON2_PKTDEC            (0x40)
#define ECON2_AUTOINC           (0x80)
#define ECON1_DMAST             (0x20)
#define ECON1_CSUMEN            (0x10)
#define ECON1_TXRTS             (0x08)
#define ECON1_RXEN              (0x04)

#define ERXFCON_CRCEN           (0x20)

#define MACON1_MARXEN           (0x01)
#define MACON1_RXPAUS           (0x04)
#define MACON1_TXPAUS           (0x08)
#define MACON3_TXCRCEN          (0x10)
#define MACON3_FULDPX           (0x01)
#define MACON4_DEFER            (0x40)
#define MICMD_MIISCAN           (0x02)
#define MISTAT_BUSY             (0x01)
#define MISTAT_NVALID           (0x04)

/*
 * Bit in high byte of 16-bit PHY register PHSTAT2
 */
#define PHSTAT2_HI_LSTAT        (0x04)

/*
 * Address of something in the ENC28J60 SRAM
 * (valid range is 0x0000 .. 0x1fff)
 */
typedef uint16_t enc28j60_addr_t;

/* ========================================================================= */

/*
 * Opcodes for SPI commands
 */
#define SPI_OPCODE_RCR(reg_desc)    (0x00 | REG(reg_desc))
#define SPI_OPCODE_WCR(reg_desc)    (0x40 | REG(reg_desc))
#define SPI_OPCODE_BFS(reg_desc)    (0x80 | REG(reg_desc))
#define SPI_OPCODE_BFC(reg_desc)    (0xA0 | REG(reg_desc))
#define SPI_OPCODE_RBM              (0x3A)
#define SPI_OPCODE_WBM              (0x7A)

/* ========================================================================= */

/*
 * Reset and initialize the controller.
 */
void
enc28j60_init(void);

/* ------------------------------------------------------------------------- */

/*
 * Ensure that the bank of the indicated register is paged in
 */
void
enc28j60_select_bank(uint16_t register_descr);

/* ------------------------------------------------------------------------- */

/*
 * Ensure that the register 'register_descr' is paged in, then write an 8-bit
 * opcode plus another 8-bit value.
 *
 * (Not intended to be used directly, use macros below.)
 */
void
enc28j60_internal_write8plus8(uint8_t opcode,
                              uint16_t register_descr,
                              uint8_t value);

/* ------------------------------------------------------------------------- */

/*
 * Write an 8-bit value to an ETH/MAC/MII register
 */
#define enc28j60_write_register(descr, value)                                 \
  enc28j60_internal_write8plus8(SPI_OPCODE_WCR(descr), descr, value)

/*
 * Set indicated bits of an ETH/MAC/MII register
 * (resulting register value = old value OR bits_to_set)
 */
#define enc28j60_bitfield_set(descr, bits_to_set)                             \
  enc28j60_internal_write8plus8(SPI_OPCODE_BFS(descr), descr, bits_to_set)

/*
 * Clear indicated bits of an ETH/MAC/MII register
 * (resulting register value = old value AND NOT bits_to_clear)
 */
#define enc28j60_bitfield_clear(descr, bits_to_clear)                         \
  enc28j60_internal_write8plus8(SPI_OPCODE_BFC(descr), descr, bits_to_clear)

/* ------------------------------------------------------------------------- */

/*
 * Return value of indicated ETH/MAC/MII register
 */
uint8_t
enc28j60_read_register(uint16_t register_descr);

/* ------------------------------------------------------------------------- */

/*
 * Poll indicated ETH/MAC/MII register R until
 *
 *   (reg & mask) == value
 *
 * If the condition is not fulfilled within a few seconds,
 * fatal_error() is called.
 */
void
enc28j60_poll_register(uint16_t register_descr,
                       uint8_t mask,
                       uint8_t value);

/* ------------------------------------------------------------------------- */

/*
 * Poll indicated ETH/MAC/MII register until indicated flag becomes cleared.
 *
 * If the flag is not cleared within a few seconds, fatal_error() is called.
 */
#define enc28j60_poll_until_clear(reg, flag)                                  \
  enc28j60_poll_register((reg), (flag), 0)


/* ------------------------------------------------------------------------- */

/*
 * Poll indicated ETH/MAC/MII register until indicated flag becomes set.
 *
 * If the flag is not set within a few seconds, fatal_error() is called.
 */
#define enc28j60_poll_until_set(reg, flag)                                  \
  enc28j60_poll_register((reg), (flag), (flag))

/* ------------------------------------------------------------------------- */

/*
 * Read a number of bytes from on-chip SRAM
 */
void
enc28j60_read_memory(uint8_t         *dst_addr,
                     enc28j60_addr_t  src_addr,
                     uint16_t         nbr_bytes);

/* ------------------------------------------------------------------------- */

/*
 * Write a number of bytes to on-chip SRAM at a given address
 */
void
enc28j60_write_memory_at(enc28j60_addr_t  dst_addr,
                         const uint8_t   *src_addr,
                         uint16_t         nbr_bytes);

/* ------------------------------------------------------------------------- */

/*
 * Write a number of bytes to on-chip SRAM, continuing after previous write
 */
void
enc28j60_write_memory_cont(const uint8_t   *src_addr,
                           uint16_t         nbr_bytes);

#endif /* SPECCYBOOT_ETH_IP_INCLUSION_GUARD */
