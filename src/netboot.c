/*
 * Module netboot:
 *
 * Loads and executes a ZX Spectrum image over TFTP.
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

#include <stddef.h>

#include "netboot.h"

#include "enc28j60.h"
#include "spectrum.h"
#include "logging.h"

/* ------------------------------------------------------------------------- */

void
netboot_do(void)
{
  logging_init();

  enc28j60_init();
  
  {
    uint8_t rev = enc28j60_read_register(EREVID);
    logging_add_entry("ENC28J60 rev. 0x\200", (uint8_t) (rev & 0x1f));
    enc28j60_write_register(ERDPTL, 0x3f);
    logging_add_entry("Read ERDPTL=0x\200, expecting 0x3f",
                      (uint8_t) enc28j60_read_register(ERDPTL));
    enc28j60_write_register(EWRPTL, 0x57);
    logging_add_entry("Read EWRPTL=0x\200, expecting 0x57",
                      (uint8_t) enc28j60_read_register(EWRPTL));
    enc28j60_write_register(MAADR5, 0x9c);
    logging_add_entry("Read MAADR5=0x\200, expecting 0x9c",
                      (uint8_t) enc28j60_read_register(MAADR5));
    enc28j60_write_register(MAADR3, 0xa3);
    logging_add_entry("Read MAADR3=0x\200, expecting 0xa3",
                      (uint8_t) enc28j60_read_register(MAADR3));

    /* now go flash some LEDs */
    enc28j60_write_register(MIREGADR, 0x14);
    for (;;) {
      enc28j60_write_register(MIWRL, 144);
      enc28j60_write_register(MIWRH, 8);
      enc28j60_write_register(MIWRL, 128);
      enc28j60_write_register(MIWRH, 9);
    }
  }
}
