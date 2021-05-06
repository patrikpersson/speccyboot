/*
 * Module enc28j60:
 *
 * Access to the Microchip ENC28J60 Ethernet host.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
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

#ifndef SPECCYBOOT_ENC28J60_INCLUSION_GUARD
#define SPECCYBOOT_ENC28J60_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

#include "util.h"
#include "spi.h"

/* -------------------------------------------------------------------------
 * ENC28J60 ETH/MAC/MII control registers
 * ------------------------------------------------------------------------- */

/*
 * Encode register info into a single byte value
 */
#define REGISTER_TUPLE_MAC_MII(bank, reg)  (((bank) * 0x20) | (0x80) | (reg))
#define REGISTER_TUPLE_ETH(bank, reg)      (((bank) * 0x20) | (reg))

#define IS_MAC_OR_MII(tuple)    ((tuple) & 0x80)
#define BANK(tuple)             (((tuple) & 0x60) >> 5)
#define REG(tuple)              ((tuple) & 0x1f)

#define ERDPTL                  REGISTER_TUPLE_ETH(0, 0x00)
#define ERDPTH                  REGISTER_TUPLE_ETH(0, 0x01)
#define EWRPTL                  REGISTER_TUPLE_ETH(0, 0x02)
#define EWRPTH                  REGISTER_TUPLE_ETH(0, 0x03)
#define ETXSTL                  REGISTER_TUPLE_ETH(0, 0x04)
#define ETXSTH                  REGISTER_TUPLE_ETH(0, 0x05)
#define ETXNDL                  REGISTER_TUPLE_ETH(0, 0x06)
#define ETXNDH                  REGISTER_TUPLE_ETH(0, 0x07)
#define ERXSTL                  REGISTER_TUPLE_ETH(0, 0x08)
#define ERXSTH                  REGISTER_TUPLE_ETH(0, 0x09)
#define ERXNDL                  REGISTER_TUPLE_ETH(0, 0x0a)
#define ERXNDH                  REGISTER_TUPLE_ETH(0, 0x0b)
#define ERXRDPTL                REGISTER_TUPLE_ETH(0, 0x0c)
#define ERXRDPTH                REGISTER_TUPLE_ETH(0, 0x0d)
#define ERXWRPTL                REGISTER_TUPLE_ETH(0, 0x0e)
#define ERXWRPTH                REGISTER_TUPLE_ETH(0, 0x0f)
#define EDMASTL                 REGISTER_TUPLE_ETH(0, 0x10)
#define EDMASTH                 REGISTER_TUPLE_ETH(0, 0x11)
#define EDMANDL                 REGISTER_TUPLE_ETH(0, 0x12)
#define EDMANDH                 REGISTER_TUPLE_ETH(0, 0x13)
#define EDMADSTL                REGISTER_TUPLE_ETH(0, 0x14)
#define EDMADSTH                REGISTER_TUPLE_ETH(0, 0x15)
#define EDMACSL                 REGISTER_TUPLE_ETH(0, 0x16)
#define EDMACSH                 REGISTER_TUPLE_ETH(0, 0x17)
#define EIE                     REGISTER_TUPLE_ETH(0, 0x1b)
#define EIR                     REGISTER_TUPLE_ETH(0, 0x1c)
#define ESTAT                   REGISTER_TUPLE_ETH(0, 0x1d)
#define ECON2                   REGISTER_TUPLE_ETH(0, 0x1e)
#define ECON1                   REGISTER_TUPLE_ETH(0, 0x1f)

#define EHT0                    REGISTER_TUPLE_ETH(1, 0x00)
#define EHT1                    REGISTER_TUPLE_ETH(1, 0x01)
#define EHT2                    REGISTER_TUPLE_ETH(1, 0x02)
#define EHT3                    REGISTER_TUPLE_ETH(1, 0x03)
#define EHT4                    REGISTER_TUPLE_ETH(1, 0x04)
#define EHT5                    REGISTER_TUPLE_ETH(1, 0x05)
#define EHT6                    REGISTER_TUPLE_ETH(1, 0x06)
#define EHT7                    REGISTER_TUPLE_ETH(1, 0x07)
#define EPMM0                   REGISTER_TUPLE_ETH(1, 0x08)
#define EPMM1                   REGISTER_TUPLE_ETH(1, 0x09)
#define EPMM2                   REGISTER_TUPLE_ETH(1, 0x0a)
#define EPMM3                   REGISTER_TUPLE_ETH(1, 0x0b)
#define EPMM4                   REGISTER_TUPLE_ETH(1, 0x0c)
#define EPMM5                   REGISTER_TUPLE_ETH(1, 0x0d)
#define EPMM6                   REGISTER_TUPLE_ETH(1, 0x0e)
#define EPMM7                   REGISTER_TUPLE_ETH(1, 0x0f)
#define EPMCSL                  REGISTER_TUPLE_ETH(1, 0x10)
#define EPMCSH                  REGISTER_TUPLE_ETH(1, 0x11)
#define EPMOL                   REGISTER_TUPLE_ETH(1, 0x14)
#define EPMOH                   REGISTER_TUPLE_ETH(1, 0x15)
#define EWOLIE                  REGISTER_TUPLE_ETH(1, 0x16)
#define EWOLIR                  REGISTER_TUPLE_ETH(1, 0x17)
#define ERXFCON                 REGISTER_TUPLE_ETH(1, 0x18)
#define EPKTCNT                 REGISTER_TUPLE_ETH(1, 0x19)

#define MACON1                  REGISTER_TUPLE_MAC_MII(2, 0x00)
#define MACON3                  REGISTER_TUPLE_MAC_MII(2, 0x02)
#define MACON4                  REGISTER_TUPLE_MAC_MII(2, 0x03)
#define MABBIPG                 REGISTER_TUPLE_MAC_MII(2, 0x04)
#define MAIPGL                  REGISTER_TUPLE_MAC_MII(2, 0x06)
#define MAIPGH                  REGISTER_TUPLE_MAC_MII(2, 0x07)
#define MACLCON1                REGISTER_TUPLE_MAC_MII(2, 0x08)
#define MACLCON2                REGISTER_TUPLE_MAC_MII(2, 0x09)
#define MAMXFLL                 REGISTER_TUPLE_MAC_MII(2, 0x0a)
#define MAMXFLH                 REGISTER_TUPLE_MAC_MII(2, 0x0b)
#define MICMD                   REGISTER_TUPLE_MAC_MII(2, 0x12)
#define MIREGADR                REGISTER_TUPLE_MAC_MII(2, 0x14)
#define MIWRL                   REGISTER_TUPLE_MAC_MII(2, 0x16)
#define MIWRH                   REGISTER_TUPLE_MAC_MII(2, 0x17)
#define MIRDL                   REGISTER_TUPLE_MAC_MII(2, 0x18)
#define MIRDH                   REGISTER_TUPLE_MAC_MII(2, 0x19)

#define MAADR5                  REGISTER_TUPLE_MAC_MII(3, 0x00)
#define MAADR6                  REGISTER_TUPLE_MAC_MII(3, 0x01)
#define MAADR3                  REGISTER_TUPLE_MAC_MII(3, 0x02)
#define MAADR4                  REGISTER_TUPLE_MAC_MII(3, 0x03)
#define MAADR1                  REGISTER_TUPLE_MAC_MII(3, 0x04)
#define MAADR2                  REGISTER_TUPLE_MAC_MII(3, 0x05)
#define EBSTSD                  REGISTER_TUPLE_ETH(3, 0x06)
#define EBSTCON                 REGISTER_TUPLE_ETH(3, 0x07)
#define EBSTCSL                 REGISTER_TUPLE_ETH(3, 0x08)
#define EBSTCSH                 REGISTER_TUPLE_ETH(3, 0x09)
#define MISTAT                  REGISTER_TUPLE_MAC_MII(3, 0x0a)
#define EREVID                  REGISTER_TUPLE_ETH(3, 0x12)
#define ECOCON                  REGISTER_TUPLE_ETH(3, 0x15)
#define EFLOCON                 REGISTER_TUPLE_ETH(3, 0x17)
#define EPAUSL                  REGISTER_TUPLE_ETH(3, 0x18)
#define EPAUSH                  REGISTER_TUPLE_ETH(3, 0x19)

/* Special register address not used by ENC28J60 */
#define ENC28J60_UNUSED_REG     (0x1a)

/* -------------------------------------------------------------------------
 * ENC28J60 PHY control registers  (datasheet table 3-3)
 * ------------------------------------------------------------------------- */

#define PHCON1                  (0x00)
#define PHSTAT2                 (0x11)

/* -------------------------------------------------------------------------
 * Individual bits in ETH, MAC, MII registers
 * ------------------------------------------------------------------------- */

#define EIE_TXIE                (0x08)
#define EIR_TXIF                (0x08)
#define EIR_TXERIF              (0x02)
#define ESTAT_CLKRDY            (0x01)
#define ESTAT_TXABRT            (0x02)
#define ECON2_PKTDEC            (0x40)
#define ECON2_AUTOINC           (0x80)
#define ECON1_TXRST             (0x80)
#define ECON1_RXRST             (0x40)
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
#define ENC_OPCODE_RCR(reg_desc)    (0x00 | REG(reg_desc))
#define ENC_OPCODE_WCR(reg_desc)    (0x40 | REG(reg_desc))
#define ENC_OPCODE_BFS(reg_desc)    (0x80 | REG(reg_desc))
#define ENC_OPCODE_BFC(reg_desc)    (0xA0 | REG(reg_desc))
#define ENC_OPCODE_RBM              (0x3A)
#define ENC_OPCODE_WBM              (0x7A)

/* FIXME rename once the one above is gone */
#define ENC_OPCODE_WCRx             (0x40)

/* ========================================================================= */

/*
 * Reset and initialize the controller.
 */
#define enc28j60_init()     __asm     SPI_RESET     __endasm

/*
 * Ensure that the bank of the indicated register is paged in. Page 0 is
 * always paged in when outside of eth.c.
 */
void
enc28j60_select_bank(uint8_t bank);

#define ENC28J60_DEFAULT_BANK        (0)

/* ------------------------------------------------------------------------- */

/*
 * Write an 8-bit opcode plus another 8-bit value.
 *
 * L holds opcode, H holds the value.
 * Destroys BC, AF.
 */
void
enc28j60_internal_write8plus8(void );
´
/* ------------------------------------------------------------------------- */

#define enc28j60_write_register16(_r, _v) \
  enc28j60_write_register16_impl(ENC_OPCODE_WCR(_r ## L), ENC_OPCODE_WCR(_r ## H), (_v))

/*
 * Write a 16-bit register, given as two register descriptors.
 * Use macro above to call.
 */
void
enc28j60_write_register16_impl(uint8_t regdesc_hi, uint8_t regdesc_lo, uint16_t value);

/* ------------------------------------------------------------------------- */

/*
 * Read value of indicated ETH/MAC/MII register.
 * The value is returned in (Z80) register C.
 */
uint8_t
enc28j60_read_register(uint8_t register_descr);

/* ------------------------------------------------------------------------- */

/*
 * Poll indicated ETH/MAC/MII register R until
 *
 *   (reg & mask) == expected_value
 *
 * If the condition is not fulfilled within a few seconds,
 * fatal_error() is called.
 *
 * Call with registers:
 *
 * H=reg
 * D=mask
 * E=expected_value
 */
void
enc28j60_poll_register(void);

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
#define enc28j60_poll_until_set(reg, flag)                                    \
  enc28j60_poll_register((reg), (flag), (flag))

/* ------------------------------------------------------------------------- */

/*
 * Read a number of bytes from on-chip SRAM, continuing from previous read.
 * The checksum is updated automatically.
 */
void
enc28j60_read_memory_cont(uint8_t *dst_addr, uint16_t nbr_bytes)
__naked;

/* ------------------------------------------------------------------------- */

/*
 * Add a number of 16-bit words to the IP-style checksum.
 */
void
enc28j60_add_checksum(const void *start_addr, uint16_t nbr_words)
__naked;

/* ------------------------------------------------------------------------- */

/*
 * Access the IP-style (16 bit one-complement) checksum updated by
 * enc28j60_read_memory_{at|cont} and enc28j60_add_checksum.
 */
#define enc28j60_get_checksum()                (enc28j60_ip_checksum)
#define enc28j60_set_checksum(_c)              (enc28j60_ip_checksum = (_c))

extern uint16_t enc28j60_ip_checksum;

/* ------------------------------------------------------------------------- */

/*
 * Read a number of bytes from a given address in on-chip SRAM.
 */
#define enc28j60_read_memory_at(_dst, _src, _n)  {                            \
  enc28j60_write_register16(ERDPT, (_src));                                   \
  enc28j60_read_memory_cont((_dst), (_n));    }

/* ------------------------------------------------------------------------- */

/*
 * Write a number of bytes to on-chip SRAM at a given address
 */
#define enc28j60_write_memory_at(_dst, _src, _n)  {                           \
  enc28j60_write_register16(EWRPT, (_dst));                                   \
  enc28j60_write_memory_cont((_src), (_n));       }

/* ------------------------------------------------------------------------- */

/*
 * Write a number of bytes to on-chip SRAM, continuing after previous write
 */
void
enc28j60_write_memory_cont(const uint8_t *src_addr, uint16_t nbr_bytes);

/* ------------------------------------------------------------------------- */

#pragma save
#pragma sdcc_hash +

/*
 * Read a number of bytes from current ERDPT to a given address.
 *
 * Destroys AF, BC, DE, HL. Does not use the stack.
 */
#define ENC28J60_READ_INLINE(_dst, _nbytes)   \
  ld   bc, #0x083a                            \
rest_appdata_cmd_loop::                       \
  SPI_WRITE_BIT_FROM(c)                       \
  djnz  rest_appdata_cmd_loop                 \
                                              \
  ld    hl, #(_nbytes)                        \
  ld    de, #(_dst)                           \
restore_appdata_loop::                        \
  ld    b, #8                                 \
restore_appdata_bit_loop::                    \
  SPI_READ_BIT_TO(c)                          \
  djnz  restore_appdata_bit_loop              \
  ld    a, c                                  \
  ld    (de), a                               \
  dec   hl                                    \
  inc   de                                    \
  ld    a, h                                  \
  or    a, l                                  \
  jr    nz, restore_appdata_loop              \
                                              \
  SPI_END_TRANSACTION

#pragma restore

#endif /* SPECCYBOOT_ENC28J60_INCLUSION_GUARD */
