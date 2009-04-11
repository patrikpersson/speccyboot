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
#ifndef SPECCYBOOT_ENC28J60_INCLUSION_GUARD
#define SPECCYBOOT_ENC28J60_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

#include "speccyboot.h"
#include "params.h"

/* ========================================================================= */

/* -------------------------------------------------------------------------
 * ENC28J60 ETH/MAC/MII control registers
 * ------------------------------------------------------------------------- */

#define REGISTER_TUPLE(is_mac_or_mii, bank, reg) \
  (is_mac_or_mii),(bank),(reg)

/*
 * Registers >= 0x1A are present in all banks, so no switching necessary
 */
#define REGISTERS_IN_ALL_BANKS               (0x1A)

#define ERDPTL                               REGISTER_TUPLE(false, 0, 0x00)
#define ERDPTH                               REGISTER_TUPLE(false, 0, 0x01)
#define EWRPTL                               REGISTER_TUPLE(false, 0, 0x02)
#define EWRPTH                               REGISTER_TUPLE(false, 0, 0x03)
#define ETXSTL                               REGISTER_TUPLE(false, 0, 0x04)
#define ETXSTH                               REGISTER_TUPLE(false, 0, 0x05)
#define ETXNDL                               REGISTER_TUPLE(false, 0, 0x06)
#define ETXNDH                               REGISTER_TUPLE(false, 0, 0x07)
#define ERXSTL                               REGISTER_TUPLE(false, 0, 0x08)
#define ERXSTH                               REGISTER_TUPLE(false, 0, 0x09)
#define ERXNDL                               REGISTER_TUPLE(false, 0, 0x0a)
#define ERXNDH                               REGISTER_TUPLE(false, 0, 0x0b)
#define ERXRDPTL                             REGISTER_TUPLE(false, 0, 0x0c)
#define ERXRDPTH                             REGISTER_TUPLE(false, 0, 0x0d)
#define ERXWRPTL                             REGISTER_TUPLE(false, 0, 0x0e)
#define ERXWRPTH                             REGISTER_TUPLE(false, 0, 0x0f)
#define EDMASTL                              REGISTER_TUPLE(false, 0, 0x10)
#define EDMASTH                              REGISTER_TUPLE(false, 0, 0x11)
#define EDMANDL                              REGISTER_TUPLE(false, 0, 0x12)
#define EDMANDH                              REGISTER_TUPLE(false, 0, 0x13)
#define EDMADSTL                             REGISTER_TUPLE(false, 0, 0x14)
#define EDMADSTH                             REGISTER_TUPLE(false, 0, 0x15)
#define EDMACSL                              REGISTER_TUPLE(false, 0, 0x16)
#define EDMACSH                              REGISTER_TUPLE(false, 0, 0x17)
#define EIE                                  REGISTER_TUPLE(false, 0, 0x1b)
#define EIR                                  REGISTER_TUPLE(false, 0, 0x1c)
#define ESTAT                                REGISTER_TUPLE(false, 0, 0x1d)
#define ECON2                                REGISTER_TUPLE(false, 0, 0x1e)
#define ECON1                                REGISTER_TUPLE(false, 0, 0x1f)

#define EHT0                                 REGISTER_TUPLE(false, 1, 0x00)
#define EHT1                                 REGISTER_TUPLE(false, 1, 0x01)
#define EHT2                                 REGISTER_TUPLE(false, 1, 0x02)
#define EHT3                                 REGISTER_TUPLE(false, 1, 0x03)
#define EHT4                                 REGISTER_TUPLE(false, 1, 0x04)
#define EHT5                                 REGISTER_TUPLE(false, 1, 0x05)
#define EHT6                                 REGISTER_TUPLE(false, 1, 0x06)
#define EHT7                                 REGISTER_TUPLE(false, 1, 0x07)
#define EPMM0                                REGISTER_TUPLE(false, 1, 0x08)
#define EPMM1                                REGISTER_TUPLE(false, 1, 0x09)
#define EPMM2                                REGISTER_TUPLE(false, 1, 0x0a)
#define EPMM3                                REGISTER_TUPLE(false, 1, 0x0b)
#define EPMM4                                REGISTER_TUPLE(false, 1, 0x0c)
#define EPMM5                                REGISTER_TUPLE(false, 1, 0x0d)
#define EPMM6                                REGISTER_TUPLE(false, 1, 0x0e)
#define EPMM7                                REGISTER_TUPLE(false, 1, 0x0f)
#define EPMCSL                               REGISTER_TUPLE(false, 1, 0x10)
#define EPMCSH                               REGISTER_TUPLE(false, 1, 0x11)
#define EPMOL                                REGISTER_TUPLE(false, 1, 0x14)
#define EPMOH                                REGISTER_TUPLE(false, 1, 0x15)
#define EWOLIE                               REGISTER_TUPLE(false, 1, 0x16)
#define EWOLIR                               REGISTER_TUPLE(false, 1, 0x17)
#define ERXFCON                              REGISTER_TUPLE(false, 1, 0x18)
#define ERXFCNT                              REGISTER_TUPLE(false, 1, 0x19)

#define MACON1                               REGISTER_TUPLE(true, 2, 0x00)
#define MACON3                               REGISTER_TUPLE(true, 2, 0x02)
#define MACON4                               REGISTER_TUPLE(true, 2, 0x03)
#define MABBIPG                              REGISTER_TUPLE(true, 2, 0x04)
#define MAIPGL                               REGISTER_TUPLE(true, 2, 0x06)
#define MAIPGH                               REGISTER_TUPLE(true, 2, 0x07)
#define MACLCON1                             REGISTER_TUPLE(true, 2, 0x08)
#define MACLCON2                             REGISTER_TUPLE(true, 2, 0x09)
#define MAMXFLL                              REGISTER_TUPLE(true, 2, 0x0a)
#define MAMXFLH                              REGISTER_TUPLE(true, 2, 0x0b)
#define MICMD                                REGISTER_TUPLE(true, 2, 0x12)
#define MIREGADR                             REGISTER_TUPLE(true, 2, 0x14)
#define MIWRL                                REGISTER_TUPLE(true, 2, 0x16)
#define MIWRH                                REGISTER_TUPLE(true, 2, 0x17)
#define MIRDL                                REGISTER_TUPLE(true, 2, 0x18)
#define MIRDH                                REGISTER_TUPLE(true, 2, 0x19)

#define MAADR5                               REGISTER_TUPLE(true, 3, 0x00)
#define MAADR6                               REGISTER_TUPLE(true, 3, 0x01)
#define MAADR3                               REGISTER_TUPLE(true, 3, 0x02)
#define MAADR4                               REGISTER_TUPLE(true, 3, 0x03)
#define MAADR1                               REGISTER_TUPLE(true, 3, 0x04)
#define MAADR2                               REGISTER_TUPLE(true, 3, 0x05)
#define EBSTSD                               REGISTER_TUPLE(false, 3, 0x06)
#define EBSTCON                              REGISTER_TUPLE(false, 3, 0x07)
#define EBSTCSL                              REGISTER_TUPLE(false, 3, 0x08)
#define EBSTCSH                              REGISTER_TUPLE(false, 3, 0x09)
#define MISTAT                               REGISTER_TUPLE(true, 3, 0x0a)
#define EREVID                               REGISTER_TUPLE(false, 3, 0x12)
#define ECOCON                               REGISTER_TUPLE(false, 3, 0x15)
#define EFLOCON                              REGISTER_TUPLE(false, 3, 0x17)
#define EPAUSL                               REGISTER_TUPLE(false, 3, 0x18)
#define EPAUSH                               REGISTER_TUPLE(false, 3, 0x19)

/* -------------------------------------------------------------------------
 * ENC28J60 PHY control registers  (datasheet table 3-3)
 * ------------------------------------------------------------------------- */

enum enc28j60_phy_reg_t {
 PHCON1  = 0x00,
 PHSTAT1 = 0x01,
 PHID1   = 0x02,
 PHID2   = 0x03,
 PHCON2  = 0x10,
 PHSTAT2 = 0x11,
 PHIE    = 0x12,
 PHIR    = 0x13,
 PHLCON  = 0x14
};

/* -------------------------------------------------------------------------
 * Type of interrupt (bitmask for return values of enc28j60_poll())
 * ------------------------------------------------------------------------- */

#define ENC28J60_INT_ACTIVE    (EN_INT)
#define ENC28J60_WOL_ACTIVE    (EN_WOL)

/* ========================================================================= */

/*
 * Address of something in the ENC28J60 SRAM
 */
typedef uint16_t enc28j60_addr_t;

/* ========================================================================= */

/* -------------------------------------------------------------------------
 * Initialize Ethernet controller
 *
 * mac_address:    MAC address for local Ethernet interface
 * ------------------------------------------------------------------------- */
void
enc28j60_init(struct mac_address_t *mac_address);

/* -------------------------------------------------------------------------
 * Read control register. Intended to be called with a register tuple as
 * defined above, e.g.,
 *
 *   v = enc28j60_read_register(MAADR4);
 *
 * is_mac_or_mii: true for MAC or MII registers, false for ETH registers
 * bank:          register bank (0..3)
 * reg:           register (0..31)
 *
 * returns register value (0..255)
 * ------------------------------------------------------------------------- */
uint8_t
enc28j60_read_register(bool    is_mac_or_mii,
                       uint8_t bank,
                       uint8_t reg);

/* -------------------------------------------------------------------------
 * Write ETH/MAC/MII control register. Intended to be called with a register
 * tuple as defined above, e.g.,
 *
 *   enc28j60_write_register(MAADR4, 0x35);
 *
 * is_mac_or_mii: true for MAC or MII registers, false for ETH registers
 * bank:          register bank (0..3)
 * reg:           register (0..31)
 * value:         new register value (8 bits)
 * ------------------------------------------------------------------------- */
void
enc28j60_write_register(bool    is_mac_or_mii,
                        uint8_t bank,
                        uint8_t reg,
                        uint8_t value);

/* -------------------------------------------------------------------------
 * Write PHY control register.
 *
 * reg:           register
 * value:         new register value (16 bits)
 * ------------------------------------------------------------------------- */
void
enc28j60_write_phy_register(enum enc28j60_phy_reg_t  reg,
                            uint16_t                 value);

/* -------------------------------------------------------------------------
 * Read on-chip SRAM.
 *
 * dst_addr:      Spectrum address to copy to
 * src_addr:      address in the ENC28J60 to copy from
 * nbr_bytes:     number of bytes to copy
 * ------------------------------------------------------------------------- */
void
enc28j60_read_memory(uint8_t *        dst_addr,
                     enc28j60_addr_t  src_addr,
                     uint16_t         nbr_bytes);

/* -------------------------------------------------------------------------
 * Write on-chip SRAM.
 *
 * dst_addr:      address in the ENC28J60 to copy to
 * src_addr:      Spectrum address to copy from
 * nbr_bytes:     number of bytes to copy
 * ------------------------------------------------------------------------- */
void
enc28j60_write_memory(enc28j60_addr_t  dst_addr,
                      uint8_t *        src_addr,
                      uint16_t         nbr_bytes);

/* -------------------------------------------------------------------------
 * Poll controller's interrupt pin status
 *
 * returns
 *   0                                           no interrupts
 *   ENC28J60_INT_ACTIVE                         INT is active
 *   ENC28J60_WOL_ACTIVE                         WOL is active
 *   ENC28J60_INT_ACTIVE + ENC28J60_WOL_ACTIVE   INT and WOL are active
 * ------------------------------------------------------------------------- */
uint8_t
enc28j60_poll(void);

#endif /* ZEB_ENC28J60_INCLUSION_GUARD */
