/*
 * Module syslog:
 *
 * BSD-style syslog support (RFC 3164)
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
#include <string.h>
#include <stdarg.h>

#include "syslog.h"
#include "udp_ip.h"

#define SYSLOG_PORT                      (514)

/* ------------------------------------------------------------------------- */

/*
 * A macro saves a few bytes of code space (in this particular case)
 */
#define hexdigit(n) ( ((n) < 10) ? ((n) + '0') : ((n) + 'a' - 10))

/*
 * Helper: append an 8-bit value in hex to the UDP payload
 */
static void
add_byte_as_hex(uint8_t b)
{
  char c = hexdigit(b >> 4);
  udp_add_variable_payload_to_packet(&c, 1);
  c = hexdigit(b & 0x0f);
  udp_add_variable_payload_to_packet(&c, 1);
}

/* ------------------------------------------------------------------------- */

void
syslog(const char *fmt, ...)
{
  /*
   *
   * http://www.ietf.org/rfc/rfc3164.txt
   *
   * Section 4:
   * "The payload of any IP packet that has a UDP destination port of 514
   *  MUST be treated as a syslog message."
   *
   * PRI and HEADER parts are optional, and it is not obvious what they would
   * hold anyway. Hence, we stick to a minimalistic format.
   *
   * To avoid an extra buffer, we calculate the length of the total packet
   * before we actually assemble it.
   */
  uint16_t packet_len = 0;
  
  const char *fmt_tmp_ptr = fmt;
  char c;
  va_list args;
  
  while(c = *fmt_tmp_ptr++) {
    if (c == '%') {
      packet_len += 3;                  /* 4 - 1 (added below) */
    }
    packet_len ++;
  }

  /*
   * Second pass: assemble the actual message
   */
  udp_create_packet(&eth_broadcast_address,
                    &ip_config.broadcast_address,
                    htons(SYSLOG_PORT),
                    htons(SYSLOG_PORT),
                    packet_len,
                    ETH_FRAME_OPTIONAL);
  
  va_start(args, fmt);
  
  while(c = *fmt++) {
    if (c == '%') {
      uint16_t value = (uint16_t) va_arg(args, int);
      
      add_byte_as_hex(HIBYTE(value));
      add_byte_as_hex(LOBYTE(value));
    }
    else {
      udp_add_payload_byte_to_packet((uint8_t) c);
    }
  }
  
  va_end(args);
  
  udp_send_packet();
}
