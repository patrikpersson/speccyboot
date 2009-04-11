/*
 * Module netboot:
 *
 * Loads and executes a ZX Spectrum image over TFTP.
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

#include <stddef.h>

#include "netboot.h"

#include "enc28j60.h"
#include "spectrum.h"
#include "params.h"
#include "logging.h"
#include "timer.h"

/* ------------------------------------------------------------------------- */

#define DISPLAY_REGISTER(REG, EXPECTED)   do {                                \
  uint8_t x[2];                                                               \
  x[0] = enc28j60_read_register(REG);                                         \
  x[1] = EXPECTED;                                                            \
  logging_add_entry("Read " #REG ": " HEX_ARG "/" HEX_ARG, x);                \
} while(0)

/* ------------------------------------------------------------------------- */

static uint8_t
checksum(uint8_t *addr, uint16_t nbr_bytes)
{
  uint8_t cs = 0;
  uint16_t i;
  for (i = 0; i < nbr_bytes; i++) {
    cs ^= *addr++;
  }
  return cs;
}
/* ------------------------------------------------------------------------- */
uint8_t read_buffer[512];

void
netboot_do(void)
{
  logging_init();

  {
    struct mac_address_t mac_address;
    
    params_get_mac_address(&mac_address);
    enc28j60_init(&mac_address);
  }
  
  {
    uint8_t x = enc28j60_read_register(EREVID) & 0x1f;
    logging_add_entry("ENC28J60 rev. 0x" HEX_ARG, &x);

    DISPLAY_REGISTER(ERXSTL, 0x00);
    DISPLAY_REGISTER(ERXSTH, 0x00);
    DISPLAY_REGISTER(ERXNDL, 0xFF);
    DISPLAY_REGISTER(ERXNDH, 0x17);
    DISPLAY_REGISTER(MAADR1, 0x12);
    DISPLAY_REGISTER(MAADR2, 0x34);
    DISPLAY_REGISTER(MAADR3, 0x56);
    DISPLAY_REGISTER(MAADR4, 0x78);
    DISPLAY_REGISTER(MAADR5, 0x9A);
    DISPLAY_REGISTER(MAADR6, 0xBC);
    
    enc28j60_write_memory(0x0100, (uint8_t *) 0x0300, sizeof(read_buffer));
    enc28j60_read_memory(read_buffer, 0x0100, sizeof(read_buffer));
    x = checksum((uint8_t *) 0x0300, sizeof(read_buffer));
    logging_add_entry("Checksum (FRAM) = 0x" HEX_ARG, &x);
    x = checksum(read_buffer, sizeof(read_buffer));
    logging_add_entry("Checksum  (RAM) = 0x" HEX_ARG, &x);
    
    /* now go flash some LEDs */
    for (;;) {
      enc28j60_write_phy_register(PHLCON, 0x0890);
      timer_delay(SECOND / 10);
      enc28j60_write_phy_register(PHLCON, 0x0980);
      timer_delay(SECOND / 10);

      if (spectrum_poll_input() == INPUT_FIRE) {
        return;
      }
    }
  }
}
