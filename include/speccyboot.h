/*
 * Speccyboot hardware-related definitions.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.org>
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

#ifndef SPECCYBOOT_ZEB_INCLUSION_GUARD
#define SPECCYBOOT_ZEB_INCLUSION_GUARD

#include <stdint.h>

#include "spectrum.h"
/* -------------------------------------------------------------------------
 * I/O ports implemented by ZEB
 * ------------------------------------------------------------------------- */

Z80_PORT(0x009f) sbt_cfg_port;        /* speccyboot config */

/* -------------------------------------------------------------------------
 * Values of bits in OUT register
 * ------------------------------------------------------------------------- */

enum speccyboot_out_port_bit_t {
  BIT_SPI_SCK   = 0,
  BIT_UNUSED_1  = 1,
  BIT_UNUSED_2  = 2,
  BIT_SPI_CS    = 3,    /* CS pin for ENC28J60 */
  BIT_FRAM_A14  = 4,    /* Address bit A14 for FRAM (bank select) */
  BIT_FRAM_CS   = 5,    /* 0=enable FRAM, 1=disable FRAM */
  BIT_SPI_RST   = 6,    /* RESET pin for ENC28J60 */
  BIT_SPI_MOSI  = 7     /* SPI data to ENC28J60 */
};

#define SPI_SCK              (1 << BIT_SPI_SCK)
#define SPI_CS               (1 << BIT_SPI_CS)
#define SPI_RST              (1 << BIT_SPI_RST)
#define SPI_MOSI             (1 << BIT_SPI_MOSI)

#define SELECT_INTERNAL_ROM  (1 << BIT_FRAM_CS)
#define SELECT_FRAM_BANK_2   (1 << BIT_FRAM_A14)

/* -------------------------------------------------------------------------
 * Values of bits in IN register
 * ------------------------------------------------------------------------- */

enum speccyboot_in_port_bit_t {
  BIT_EN_WOL    = 0,    /* WOL pin from ENC28J60 */    
  BIT_SPI_MISO  = 1,    /* SPI data from ENC28J60 */
  BIT_EN_INT    = 2     /* INT pin from ENC28J60 */
};

#define SPI_MISO             (1 << BIT_SPI_MISO)
#define SPI_RST              (1 << BIT_SPI_RST)
#define SPI_MOSI             (1 << BIT_SPI_MOSI)

#endif /* SPECCYBOOT_ZEB_INCLUSION_GUARD */
