/*
 * Module tftp:
 *
 * Trivial File Transfer Protocol (TFTP, RFC 1350)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
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

#include "tftp.h"

#include "eth.h"
#include "file_loader.h"
#include "globals.h"
#include "syslog.h"
#include "ui.h"

/* ------------------------------------------------------------------------- */

/* TFTP opcodes */
#define TFTP_OPCODE_RRQ           (1)
#define TFTP_OPCODE_DATA          (3)
#define TFTP_OPCODE_ACK           (4)
#define TFTP_OPCODE_ERROR         (5)

/* Size of a TFTP ACK */
#define TFTP_SIZE_OF_ACK          (4)

/* ------------------------------------------------------------------------- */

/* Next TFTP block we expect to receive */
static uint16_t expected_tftp_block_no;

/* Source port currently used by server */
static uint16_t server_port;

/* Opcode for ACK */
static const uint16_t ack_opcode = htons(TFTP_OPCODE_ACK);

/* Opcode + path for RRQ */
static const uint8_t rrq_prefix[] = {
  0, TFTP_OPCODE_RRQ,   /* opcode in network order */
  's', 'p', 'e', 'c', 'c', 'y', 'b', 'o', 'o', 't', '/'  /* no NUL! */
};

/* Transfer type for RRQ */
static const uint8_t rrq_option[] = "octet";

/* TFTP ERROR packet */
static const uint8_t error_packet[] = {
  0, TFTP_OPCODE_ERROR, /* opcode in network order */
  0, 4,                 /* error 'illegal TFTP operation', network order */
  0                     /* no particular message */
};

/* ------------------------------------------------------------------------- */

void
tftp_receive(void)
{
  uint16_t received_block_no = ntohs(rx_frame.udp.app.tftp.header.block_no);

  if (rx_frame.udp.app.tftp.header.opcode != ntohs(TFTP_OPCODE_DATA)) {
    fatal_error(FATAL_FILE_NOT_FOUND); /* ERROR, RRQ, WRQ, ACK: all wrong */
  }

  if (   received_block_no > expected_tftp_block_no
      || received_block_no < (expected_tftp_block_no - 1)
      || (expected_tftp_block_no != 1
	  && rx_frame.udp.header.src_port != server_port))
  {
    udp_create_reply(sizeof(struct udp_header_t) + sizeof(error_packet), false);
    udp_add(error_packet);
    udp_send();
    return;
  }

  server_port = rx_frame.udp.header.src_port;

  /* ACK */
  udp_create_reply(sizeof(struct udp_header_t) + TFTP_SIZE_OF_ACK, false);
  udp_add(ack_opcode);
  udp_add(rx_frame.udp.app.tftp.header.block_no);
  udp_send();

  if (received_block_no == expected_tftp_block_no) {
    expected_tftp_block_no ++;
    receive_file_data();
  }
}

/* ------------------------------------------------------------------------- */

void
tftp_read_request(const char *filename)
{
  uint8_t len = 1;  /* include filenames's NUL */
  const char *p = filename;

  while (*p++) {
    len ++;
  }

  expected_tftp_block_no = 1;
  new_tftp_client_port();

  udp_create(&eth_broadcast_address,
             &ip_config.tftp_server_address,
             tftp_client_port,
             htons(UDP_PORT_TFTP_SERVER),
             sizeof(struct udp_header_t)
               + sizeof(rrq_prefix)
               + sizeof(rrq_option)
               + len,
             ETH_FRAME_PRIORITY);
  udp_add(rrq_prefix);
  udp_add_w_len(filename, len);
  udp_add(rrq_option);
  udp_send();
}
