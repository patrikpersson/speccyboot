/*
 * Module syslog:
 *
 * BSD-style syslog support (RFC 3164)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
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

#include "syslog.h"

#include "udp_ip.h"

/* ------------------------------------------------------------------------- */

/* Message prefix, as specified in RFC 3164:
 *
 * PRI:        kernel level, informational message
 * HEADER:     none (optional, we don't know neither hostname nor timestamp)
 * TAG:        "speccyboot"
 */
static const char syslog_prefix[] = {
  '<', '6', '>',
  's', 'p', 'e', 'c', 'c', 'y', 'b', 'o', 'o', 't',
  ':', ' ' /* no trailing NUL */
};

/* ------------------------------------------------------------------------- */

void
syslog(const char *msg)
{
  uint8_t msg_length = 0;

  const char *p = msg;

  while (*p++) {
    msg_length ++;
  }

  udp_create(&eth_broadcast_address,
      	     IP_BROADCAST_ADDRESS_PTR,
      	     htons(UDP_PORT_SYSLOG),
      	     htons(UDP_PORT_SYSLOG),
      	     msg_length
      	       + sizeof(struct udp_header_t)
      	       + sizeof(syslog_prefix),
      	     ETH_FRAME_OPTIONAL);
  udp_add(syslog_prefix);
  udp_add_w_len(msg, msg_length);
  udp_send();
}
