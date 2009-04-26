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

#include <stddef.h>

#include "netboot.h"

#include "dhcp.h"
#include "tftp.h"

#include "spectrum.h"
#include "logging.h"

/* ------------------------------------------------------------------------- */

void
netboot_do(void)
{
  logging_init();
  
  eth_init();
  dhcp_init();
  
  eth_handle_incoming_frames();
}

/* ------------------------------------------------------------------------- */

/*
 * Called by DHCP (see dhcp.h) when an IP address is available
 */
void
netboot_notify_ip_ready(void)
{
  logging_add_entry("BOOT: DHCP got IP "
                    DEC8_ARG "." DEC8_ARG "." DEC8_ARG "." DEC8_ARG,
                    (uint8_t *) &ip_config.host_address);
  
  tftp_read_request("test.scr");
}

/* ------------------------------------------------------------------------- */

/*
 * Called by TFTP (see tftp.h) when data is received
 */
void
netboot_notify_tftp_data(const uint8_t *received_data,
                         uint16_t       nbr_of_bytes_received,
                         bool           more_data_expected)
{
  static uint8_t *dst = (uint8_t *) 0x4000;
  /*
  logging_add_entry("BOOT: got 0x" HEX16_ARG " bytes",
                    (uint8_t *) &nbr_of_bytes_received);
  logging_add_entry("BOOT: more=" HEX8_ARG,
                    (uint8_t *) &more_data_expected);
   */

  while (nbr_of_bytes_received-- > 0) {
    *dst ++ = *received_data++;
  }
  
  if (! more_data_expected) {
    __asm
    
      di
      ld  a, #3
      out (254), a
      
      halt
    
    __endasm;
  }
  
  (void) received_data;
}
