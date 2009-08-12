/*
 * Module tftp:
 *
 * Trivial File Transfer Protocol (TFTP, RFC 1350)
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
#include <string.h>           /* strlen */

#include "rxbuffer.h"

#include "tftp.h"

#include "syslog.h"

#include "eth.h"

/* ------------------------------------------------------------------------- */

/*
 * Supported TFTP opcodes
 */
#define TFTP_OPCODE_RRQ           (1)
#define TFTP_OPCODE_DATA          (3)
#define TFTP_OPCODE_ACK           (4)
#define TFTP_OPCODE_ERROR         (5)

/*
 * A single error: "illegal operation"
 */
#define TFTP_ERROR_ILLEGAL        (4)

/*
 * Size of a TFTP ACK
 */
#define TFTP_SIZE_OF_ACK          (4)

/*
 * Size of the file prefix, excluding NUL
 */
#define TFTP_SIZE_OF_PREFIX       (sizeof(TFTP_FILE_PREFIX) - 1)

/*
 * Error message in an outgoing TFTP ERROR packet
 */
#define TFTP_ERROR_MESSAGE        "DATA block out of sequence"

/* =========================================================================
 * TFTP packets
 * ========================================================================= */

static const PACKED_STRUCT(tftp_error_packet_t) {
  uint16_t        opcode;
  uint16_t        error_code;
  char            msg[sizeof(TFTP_ERROR_MESSAGE)];
} tftp_error_packet = {
  htons(TFTP_OPCODE_ERROR),
  htons(TFTP_ERROR_ILLEGAL),
  TFTP_ERROR_MESSAGE
};

/* =========================================================================
 * Next TFTP block we expect to receive
 * ========================================================================= */

static uint16_t expected_tftp_block_no;

/* ------------------------------------------------------------------------- */

/*
 * Data for outgoing packets
 */
static const uint16_t ack_opcode   = htons(TFTP_OPCODE_ACK);
static const uint16_t rrq_opcode   = htons(TFTP_OPCODE_RRQ);
static const uint8_t  rrq_option[] = "octet";

/* ------------------------------------------------------------------------- */

void
tftp_packet_received(uint16_t nbr_bytes_in_packet)
{
  switch (rx_frame.udp.app.tftp.opcode) {
    case ntohs(TFTP_OPCODE_DATA):
      if (ntohs(rx_frame.udp.app.tftp.block_no) > expected_tftp_block_no) {
        /*
         * A packet out of sequence: send a TFTP error packet, then panic
         */
        udp_create_reply(sizeof(tftp_error_packet));
        udp_add_payload_to_packet(tftp_error_packet);
        udp_send_packet();
        
        fatal_error(tftp_error_packet.msg);
      }
      
      /*
       * Send ACK
       */
      udp_create_reply(TFTP_SIZE_OF_ACK);
      udp_add_payload_to_packet(ack_opcode);
      udp_add_payload_to_packet(rx_frame.udp.app.tftp.block_no);
      udp_send_packet();
      
      {
        uint16_t nbr_bytes_data = nbr_bytes_in_packet
                                  - offsetof(struct tftp_data_packet_t, data);
        
        if (ntohs(rx_frame.udp.app.tftp.block_no) == expected_tftp_block_no) {
          /*
           * At this point, the callback may invoke tftp_read_request(),
           * initiating a new TFTP transfer. Therefore, we must increase
           * expected_tftp_block_no _before_ the callback, otherwise we may
           * inadvertedly increase a reset counter.
           */
          expected_tftp_block_no ++;

          NOTIFY_TFTP_DATA(rx_frame.udp.app.tftp.data,
                           nbr_bytes_data,
                           (nbr_bytes_data == TFTP_DATA_MAXSIZE));
        }
      }
      break;
      
    case ntohs(TFTP_OPCODE_ERROR):
      NOTIFY_TFTP_ERROR();
      break;
  }
}
  
/* ------------------------------------------------------------------------- */

void
tftp_read_request(const char *filename)
{
  const uint16_t      filename_len   = strlen(filename) + 1 /* NUL */;
  const uint16_t         total_len   = sizeof(rrq_opcode)
                                       + sizeof(rrq_option)
                                       + TFTP_SIZE_OF_PREFIX
                                       + filename_len;
  
  udp_new_tftp_port();
  
  expected_tftp_block_no = 1;

  /*
   * At this point, we don't yet know the address of the TFTP server, so
   * the RRQ packet is broadcast.
   */
  udp_create_packet(&eth_broadcast_address,
                    &ip_config.broadcast_address,
                    udp_port_tftp_client,
                    htons(UDP_PORT_TFTP_SERVER),
                    total_len,
                    ETH_FRAME_PRIORITY);
  udp_add_payload_to_packet(rrq_opcode);
  udp_add_variable_payload_to_packet(TFTP_FILE_PREFIX, TFTP_SIZE_OF_PREFIX);
  udp_add_variable_payload_to_packet(filename, filename_len);
  udp_add_payload_to_packet(rrq_option);
  udp_send_packet();
}
