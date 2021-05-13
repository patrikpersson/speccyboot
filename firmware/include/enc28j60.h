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
 * Ensure that register bank 0 is paged in.
 *
 * Destroys AF, BC, E, HL.
 */
void
enc28j60_select_bank0(void);

/*
 * Ensure that the bank in register E (0..3) is paged in. Page 0 is
 * always paged in when outside of eth.c.
 *
 * Destroys AF, BC, HL.
 */
void
enc28j60_select_bank(void);

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

/*
 * Write a 16-bit register.
 *
 * Call with A set to a WCR opcode for the low half of the register,
 * e.g., ENC_OPCODE_WCR(ERXRDPTL), and HL set to the 16-bit value to write.
 *
 * Destroys F, BC, DE.
 */
void
enc28j60_write_register16(void);

/* ------------------------------------------------------------------------- */

/*
 * Read value of indicated ETH/MAC/MII register.
 *
 * On entry, E should hold the register descriptor for the register to read.
 * Returns the read value in C.
 * Destroys AF, B. B will be zero on exit.
 */
void
enc28j60_read_register(void);

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
 * E=reg
 * H=mask
 * L=expected_value
 *
 * Destroys AF, BC
 */
void
enc28j60_poll_register(void);

/* ------------------------------------------------------------------------- */

/*
 * Read a number of bytes from on-chip SRAM, continuing from previous read.
 * The checksum is updated automatically.
 *
 * Call with
 * HL = destination address (buffer to read data into=
 * BC = number of bytes to read
 *
 * Destroys all registers, including alternate ones, except IX/IY.
 */
void
enc28j60_read_memory_cont(void);

/* ------------------------------------------------------------------------- */

/*
 * Add a number of 16-bit words to the IP-style checksum.
 *
 * On entry:
 *   IY = pointer to 16-words
 *   BC = number of 16-bit words (!) to add
 *
 * On exit:
 *   BC == 0
 *   IY points to next byte after checksummed data
 */
void
enc28j60_add_checksum(void);

/* ------------------------------------------------------------------------- */

/*
 * Write six bytes to on-chip SRAM, continuing after previous write.
 *
 * Call with HL=pointer to data. Destroys AF, BC, DE.
 *
 * On exit, DE==0, and HL points to the next byte after the written data.
 */
void
enc28j60_write_6b(void);

/* ------------------------------------------------------------------------- */

/*
 * Write a number of bytes to on-chip SRAM, continuing after previous write.
 *
 * Call with HL=pointer to data, DE=length of data. Destroys AF, BC.
 *
 * On exit, DE==0, and HL points to the next byte after the written data.
 */
void
enc28j60_write_memory_cont(void);

#endif /* SPECCYBOOT_ENC28J60_INCLUSION_GUARD */
