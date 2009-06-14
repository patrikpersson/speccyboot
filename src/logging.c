/*
 * Module logging:
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

#include "logging.h"
#include "udp.h"

#ifdef UDP_LOGGING

#define UDP_LOGGING_TIMESTAMP            "Jul  1 12:34:56"
#define UDP_LOGGING_HOSTNAME             "spectrum"

#define SYSLOG_PORT                      (514)

/*
 * Exclude the trailing NUL character from the length
 */
#define UDP_LOGGING_TIMESTAMP_LENGTH     (sizeof(UDP_LOGGING_TIMESTAMP) - 1)
#define UDP_LOGGING_HOSTNAME_LENGTH      (sizeof(UDP_LOGGING_HOSTNAME) - 1)

/* ------------------------------------------------------------------------- */

static const ipv4_address_t log_server_address = UDP_LOGGING_SERVER_IP_ADDRESS;

/* ------------------------------------------------------------------------- */

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
_log_udp_msg(char severity, const char *tag, const char *fmt, ...)
{
  /*
   * To avoid an extra buffer, we calculate the length of the total packet
   * before we actually assemble it.
   */
  uint16_t packet_len = 3 /* PRI: <x> */
                      + UDP_LOGGING_TIMESTAMP_LENGTH
                      + 1 /* space */
                      + UDP_LOGGING_HOSTNAME_LENGTH
                      + 1 /* space */
                      + strlen(tag)
                      + 2 /* colon + space */;
  
  const char *fmt_ptr = fmt;
  
  va_list args;
  va_start(args, fmt);
  
  while(*fmt_ptr) {
    char c = *fmt_ptr++;
    
    switch (c) {
      case '%':
      {
        switch (*fmt_ptr++) {
          case 'x':
            (void) va_arg(args, int);
            
            packet_len += 4;
            break;
          case 'a':
            (void) va_arg(args, const ipv4_address_t *);
            
            packet_len += 11;
            break;
          case 's':
            packet_len += strlen(va_arg(args, const char *));
            break;
          case 'b':
            (void) va_arg(args, int);
            
            /* fall-through */
          default:
            packet_len += 2;    /* '%' and one char more */
            break;
        }
        break;
      }
      default:
        packet_len ++;
    }
  }
  
  va_end(args);
  va_start(args, fmt);

  /*
   * Second pass: assemble the actual message
   */
  fmt_ptr = fmt;

  udp_create_packet(&eth_broadcast_address,
                    &log_server_address,
                    htons(SYSLOG_PORT),
                    htons(SYSLOG_PORT),
                    packet_len,
                    ETH_FRAME_OPTIONAL);

  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('<'), 1);
  udp_add_variable_payload_to_packet(&severity, 1);
  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('>'), 1);
  udp_add_variable_payload_to_packet(UDP_LOGGING_TIMESTAMP,
                                     UDP_LOGGING_TIMESTAMP_LENGTH);
  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR(' '), 1);
  udp_add_variable_payload_to_packet(UDP_LOGGING_HOSTNAME,
                                     UDP_LOGGING_HOSTNAME_LENGTH);
  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR(' '), 1);
  udp_add_variable_payload_to_packet(tag, strlen(tag));
  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR(':'), 1);
  udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR(' '), 1);
  
  while(*fmt_ptr) {
    char c = *fmt_ptr++;
    
    switch (c) {
      case '%':
      {
        c = *fmt_ptr ++;
        switch (c) {
          case 'x':
          {
            uint16_t value = (uint16_t) va_arg(args, int);
            
            add_byte_as_hex(HIBYTE(value));
            add_byte_as_hex(LOBYTE(value));
          }
            break;
            
          case 'b':
            add_byte_as_hex((uint8_t) va_arg(args, int));
            break;
            
          case 'a':
          {
            const uint8_t *p = va_arg(args, const uint8_t *);
            
            add_byte_as_hex(*p++);
            udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('.'), 1);
            add_byte_as_hex(*p++);
            udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('.'), 1);
            add_byte_as_hex(*p++);
            udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('.'), 1);
            add_byte_as_hex(*p);
          }
            break;
          case 's':
          {
            const char *s = va_arg(args, const char *);
            udp_add_variable_payload_to_packet(s, strlen(s));
          }
            break;
          default:
            udp_add_variable_payload_to_packet(ADDRESS_OF_CHAR('%'), 1);
            udp_add_variable_payload_to_packet(&c, 1);
            break;
        }
        break;
      }
      default:
        udp_add_variable_payload_to_packet(&c, 1);
    }
  }
  
  va_end(args);
  
  udp_send_packet(packet_len,
                  ETH_FRAME_OPTIONAL);
}

#endif /* UDP_LOGGING */