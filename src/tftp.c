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

#include "tftp.h"

#include "logging.h"

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
 * TFTP DATA packets have a maximal size of 512 bytes, unless options are set
 * by the client (this client won't)
 */
#define TFTP_DATA_MAXSIZE         (512)

/*
 * Size of a TFTP ACK
 */
#define TFTP_SIZE_OF_ACK          (4)

/* =========================================================================
 * TFTP packets
 * ========================================================================= */

PACKED_STRUCT(tftp_data_packet_t) {
  uint16_t        opcode;                   /* opcode == TFTP_OPCODE_DATA */
  uint16_t        block_no;
  uint8_t         data[TFTP_DATA_MAXSIZE];
};

PACKED_STRUCT(tftp_error_packet_t) {
  uint16_t        opcode;                   /* opcode == TFTP_OPCODE_ERROR */
  uint16_t        error_code;               /* TFTP_ERROR_ILLEGAL */
  char            error_msg_nul;            /* NUL -- no error message  */
};

/*
 * Union to describe TFTP packets we expect to receive
 */
union tftp_packet_t {
  uint16_t                   opcode;
  struct tftp_data_packet_t  data;
  struct tftp_error_packet_t error;
};

/* -------------------------------------------------------------------------
 * TFTP ERROR, sent if we get something bad from the server.
 * ------------------------------------------------------------------------- */

static const struct tftp_error_packet_t tftp_error_packet = {
  htons(TFTP_OPCODE_ERROR),
  htons(TFTP_ERROR_ILLEGAL),
  '\000'
};

/* =========================================================================
 * Next TFTP block we expect to receive
 * ========================================================================= */

static uint16_t expected_tftp_block_no = 1;

/* ------------------------------------------------------------------------- */

/*
 * Returns a pseudo-random value in the range 0x8000..0xFFFF, as a
 * network-endian 16-bit number.
 */
static uint16_t
tftp_select_port(void)
__naked
{
  __asm

    ;; Set HL to the return value
  
    ld  a, r
    or  #0x80     ;; bit 7 is not random anyway
    ld  l, a
    ld  h, a
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
tftp_packet_received(const struct mac_address_t  *src_hwaddr,
                     const ipv4_address_t        *src,
                     uint16_t                     src_port_nw_order,
                     const union tftp_packet_t   *packet,
                     uint16_t                     nbr_bytes_in_packet)
{
  static const uint16_t ack_opcode = htons(TFTP_OPCODE_ACK);
  bool data_is_fresh = true;
  
  if (packet->opcode == ntohs(TFTP_OPCODE_DATA)) {
    if (packet->data.block_no != htons(expected_tftp_block_no)) {
      if (ntohs(packet->data.block_no) == (expected_tftp_block_no - 1)) {
        /*
         * Special case: DATA packet with the previous block ID. Seems our ACK
         * got lost somewhere. Send another ack, but ignore the data.
         */
        data_is_fresh = false;
      }
      else {
        /*
         * Received a DATA packet with a bad block ID
         */
        logging_add_entry("TFTP: bad block: want " HEX16_ARG,
                          (uint8_t *) &expected_tftp_block_no);
        logging_add_entry("TFTP: bad block: got" HEX8_ARG HEX8_ARG,
                          (uint8_t *) &packet->data.block_no);
        /*
         * Send a TFTP error packet, "illegal TFTP operation"
         */
        udp_create_packet(src_hwaddr,
                          src,
                          udp_get_tftp_port(),
                          src_port_nw_order,
                          sizeof(tftp_error_packet));
        udp_add_payload_to_packet(tftp_error_packet);
        udp_send_packet(sizeof(tftp_error_packet));

        return;
      }
    }

    /*
     * Received a DATA packet with the expected block ID. Acknowledge
     * and consume it.
     */
    {
      uint16_t nbr_bytes_data 
        = nbr_bytes_in_packet - offsetof(struct tftp_data_packet_t, data);
      bool more_data = (nbr_bytes_data == TFTP_DATA_MAXSIZE);
      
      udp_create_packet(src_hwaddr,
                        src,
                        udp_get_tftp_port(),
                        src_port_nw_order,
                        TFTP_SIZE_OF_ACK);
      udp_add_payload_to_packet(ack_opcode);
      udp_add_payload_to_packet(packet->data.block_no);
      udp_send_packet(TFTP_SIZE_OF_ACK);
      
      if (data_is_fresh) {
        NOTIFY_TFTP_DATA(packet->data.data, nbr_bytes_data, more_data);
        
        if (more_data) {
          expected_tftp_block_no ++;
        }
        else {
          /*
           * All data received, stop bothering the TFTP server
           */
          eth_reset_retransmission_timer();
        }
      }
    }
  }
  else {
    logging_add_entry("TFTP: unexpected opcode", NULL);
  }
}

/* ------------------------------------------------------------------------- */

void
tftp_read_request(const char *filename)
{
  static const uint16_t rrq_opcode   = htons(TFTP_OPCODE_RRQ);
  static const uint8_t  rrq_option[] = "octet";
  const uint16_t      filename_len   = strlen(filename) + 1 /* NUL */;
  const uint16_t         total_len   = sizeof(rrq_opcode)
                                       + sizeof(rrq_option)
                                       + filename_len;
  
  udp_set_tftp_port(tftp_select_port());
  expected_tftp_block_no = 1;
  
  /*
   * At this point, we don't yet know the address of the TFTP server, so
   * the RRQ packet is broadcast.
   */
  udp_create_packet(&eth_broadcast_address,
                    &ip_config.broadcast_address,
                    udp_get_tftp_port(),
                    htons(UDP_PORT_TFTP_SERVER),
                    total_len);
  udp_add_payload_to_packet(rrq_opcode);
  udp_add_variable_payload_to_packet(filename, filename_len);
  udp_add_payload_to_packet(rrq_option);
  udp_send_packet(total_len);
}
