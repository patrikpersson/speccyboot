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

/*
 * Value for 'current_server_src_port' indicating no current session
 */
#define TFTP_SRC_PORT_NONE        (0)

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

/* =========================================================================
 * Source port used by server in current session, 
 * ========================================================================= */

static uint16_t current_server_src_port;

/* =========================================================================
 * Address of TFTP server. Defaults to broadcast, updated when the server
 * responds.
 * ========================================================================= */

static PACKED_STRUCT() {
  struct mac_address_t  mac_addr;
  ipv4_address_t        ip_addr;
  bool                  valid;
} tftp_server_addr = {
  BROADCAST_MAC_ADDR,
  IP_DEFAULT_BCAST_ADDRESS,
  false
};

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
  switch (rx_frame.udp.app.tftp.header.opcode) {
    case ntohs(TFTP_OPCODE_DATA):
      if ((ntohs(rx_frame.udp.app.tftp.header.block_no) > expected_tftp_block_no)
          || (current_server_src_port != TFTP_SRC_PORT_NONE
              && (current_server_src_port != rx_frame.udp.header.src_port)))
      {
        /*
         * A packet out of sequence, or a packet from another session (earlier
         * failures could lead re-transmissions for old sessions):
         * send a TFTP error packet
         */
        udp_create_reply(sizeof(tftp_error_packet));
        udp_add_payload_to_packet(tftp_error_packet);
        udp_send_packet();
        
        return;
      }
      
      /*
       * If we didn't already know the TFTP server address, we do now
       */
      if (! tftp_server_addr.valid) {
        uint8_t i;
        for (i = 0; i < sizeof(struct mac_address_t); i++) {
          tftp_server_addr.mac_addr.addr[i]
          = rx_eth_adm.eth_header.src_addr.addr[i];
        }
        
        tftp_server_addr.ip_addr  = rx_frame.ip.src_addr;
        tftp_server_addr.valid    = true;
      }
      
      /*
       * If we didn't already know the source port for this session, we do now
       *
       * NOTE: this is different from the server address above, because that
       *       address is permanent over all sessions, but the source port
       *       value is reset for each session.
       */
      if (current_server_src_port == TFTP_SRC_PORT_NONE) {
        current_server_src_port = rx_frame.udp.header.src_port;
      }
      
      /*
       * Send ACK
       */
      udp_create_reply(TFTP_SIZE_OF_ACK);
      udp_add_payload_to_packet(ack_opcode);
      udp_add_payload_to_packet(rx_frame.udp.app.tftp.header.block_no);
      udp_send_packet();
      
      {
        uint16_t nbr_bytes_data = nbr_bytes_in_packet
                                  - sizeof(struct tftp_header_t);
        
        if (ntohs(rx_frame.udp.app.tftp.header.block_no) == expected_tftp_block_no) {
          /*
           * At this point, the callback may invoke tftp_read_request(),
           * initiating a new TFTP transfer. Therefore, we must increase
           * expected_tftp_block_no _before_ the callback, otherwise we may
           * inadvertedly increase a reset counter.
           */
          expected_tftp_block_no ++;

          NOTIFY_TFTP_DATA(rx_frame.udp.app.tftp.data.raw_bytes,
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
  
  /*
   * Broadcast the TFTP RRQ, unless we know about a better server address
   */
  const ipv4_address_t *ip_addr        = &ip_config.broadcast_address;
  const struct mac_address_t *mac_addr = &eth_broadcast_address;
  
  if (tftp_server_addr.valid) {
    ip_addr  = &tftp_server_addr.ip_addr;
    mac_addr = &tftp_server_addr.mac_addr;
  }
  
  udp_new_tftp_port();
  
  expected_tftp_block_no  = 1;
  current_server_src_port = TFTP_SRC_PORT_NONE;
  
  udp_create_packet(mac_addr,
                    ip_addr,
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
