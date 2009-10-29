/*
 * Module eth:
 *
 * Ethernet implementation using the Microchip ENC28J60 Ethernet host.
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
#ifndef SPECCYBOOT_ETH_INCLUSION_GUARD
#define SPECCYBOOT_ETH_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

#include "enc28j60.h"
#include "util.h"

/* -------------------------------------------------------------------------
 * ENC28J60 buffer management
 * ------------------------------------------------------------------------- */

/*
 * Administrative overhead in transmission buffer
 * (1 per-packet control byte, 7 bytes transmission status vector)
 */
#define ENC28J60_TX_ADM         (1 + 7)

/*
 * Administrative overhead in received frames
 */
#define ENC28J60_RX_ADM         (4)

/*
 * Worst-case frame size
 */
#define MAX_FRAME_SIZE          (sizeof(struct eth_header_t) + IP_MAX_PAYLOAD)

/*
 * Transmission buffer size:
 * Ethernet header, payload, and administrative info stored by controller
 */
#define ENC28J60_TXBUF_SIZE     (MAX_FRAME_SIZE + ENC28J60_TX_ADM)

/*
 * Receive buffer: no header (stored separately), receive status vector
 */
#define ENC28J60_RXBUF_SIZE     (IP_MAX_PAYLOAD + ENC28J60_RX_ADM)

/*
 * MEMORY MAP
 * ==========
 *
 * Errata for silicon rev. B5, item #3: receive buffer must start at 0x0000
 *
 * 0x0000 ... 0x0FFF    4K receive buffer (FIFO): automatically filled with
 *                      received packets by the ENC28J60. The host updates
 *                      ERXRDPT to inform the ENC28J60 when data is consumed.
 *
 * 0x1000 ... 0xXXXX    TX buffer 1. This is the important one -- used for
 *                      frame class CRITICAL. On time-outs, this frame will be
 *                      re-transmitted (if valid).
 *
 * 0xXXXX+1...0xYYYY    TX buffer 2. This buffer is used for frames where no
 *                      reply is expected -- frame class OPTIONAL.
 *
 * 0x1600 ... 0x1FFF    Reserved for temporary storage during snapshot
 *                      loading (see context_switch.c)
 *
 * Also see the comment for eth_frame_class_t (eth.h).
 */

#define ENC28J60_RXBUF_START    (0x0000)
#define ENC28J60_RXBUF_END      (0x0FFF)
#define ENC28J60_TXBUF1_START   (0x1000)
#define ENC28J60_TXBUF1_END     (ENC28J60_TXBUF1_START + ENC28J60_TXBUF_SIZE-1)
#define ENC28J60_TXBUF2_START   (ENC28J60_TXBUF1_START + ENC28J60_TXBUF_SIZE)

/* ========================================================================= */

/*
 * Broadcast MAC address
 */
#define BROADCAST_MAC_ADDR      { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } }

/* ========================================================================= */

/*
 * Define as struct to avoid the usual pointer vs array confusion. (This is
 * especially confusing with a typedef.)
 *
 * The 'addr' element is first in the struct, so casting a uint8_t pointer
 * to a struct mac_address_t pointer is guaranteed to be OK.
 */

PACKED_STRUCT(mac_address_t) {
  uint8_t addr[6];      /* 48 bits */
};

/*
 * Ethernet and administrative data, as written by ENC28J60 reception logic
 * (datasheet, 7.2.2)
 */
PACKED_STRUCT(eth_adm_t) {
  enc28j60_addr_t       next_ptr;    /* written as little-endian by ENC28J60 */
  uint16_t              nbr_bytes;   /* written as little-endian by ENC28J60 */
  uint8_t               rsv16to23;
  uint8_t               rsv24to31;
  
  /*
   * Ethernet header
   * http://en.wikipedia.org/wiki/Ethernet
   */
  PACKED_STRUCT(eth_header_t) {
    struct mac_address_t  dst_addr;
    struct mac_address_t  src_addr;
    uint16_t              ethertype;
  } eth_header;
};

/* ========================================================================= */

/*
 * Ethernet HW type (as used by, e.g., ARP)
 */
#define ETH_HWTYPE                  (1)

/*
 * Two classes of frames:
 *
 * PRIORITY       Automatically re-transmitted when a timer expires. If
 *                another PRIORITY frame is transmitted, the timer is reset.
 *
 *                This means frames where we care about an answer
 *                (DHCP, TFTP).
 *
 * OPTIONAL       Not automatically re-transmitted. The timer is not affected
 *                in any way. The frame may be silently dropped if the
 *                on-chip storage is in use for other stuff -- see
 *                eth_store_data().
 *
 *                This means frames where we do NOT care about an answer
 *                (ARP replies, syslog).
 *
 * NOTE: the frame class value actually maps directly to a transmission buffer
 *       address.
 */
typedef enc28j60_addr_t eth_frame_class_t;

#define ETH_FRAME_PRIORITY      (ENC28J60_TXBUF1_START)
#define ETH_FRAME_OPTIONAL      (ENC28J60_TXBUF2_START)
 
/* ========================================================================= */

/* -------------------------------------------------------------------------
 * Ethernet addresses
 * ------------------------------------------------------------------------- */

extern const struct mac_address_t eth_broadcast_address;
extern const struct mac_address_t eth_local_address;

/* -------------------------------------------------------------------------
 * Initialize Ethernet layer
 * ------------------------------------------------------------------------- */
void
eth_init(void);

/* -------------------------------------------------------------------------
 * Create an Ethernet frame for transmission. If a previous frame is
 * currently in transmission, block until that is done first.
 *
 * destination:           destination MAC address, or eth_broadcast_address
 * ethertype:             value for length/type field
 * frame_class:           see comment for enum eth_frame_class_t above
 * ------------------------------------------------------------------------- */
void
eth_create_frame(const struct mac_address_t *destination,
                 uint16_t                    ethertype,
                 eth_frame_class_t           frame_class);

/* -------------------------------------------------------------------------
 * Append payload to a frame previously created with eth_create_frame().
 * ------------------------------------------------------------------------- */
#define eth_add_payload_to_frame(_pld, _n)                                    \
  enc28j60_write_memory_cont((const uint8_t *) (_pld), (_n))

/* -------------------------------------------------------------------------
 * Append a byte-size payload to a frame previously created with
 * eth_create_frame().
 * ------------------------------------------------------------------------- */
void
eth_add_payload_byte_to_frame(uint8_t b);

/* -------------------------------------------------------------------------
 * Append a 16-bit network-order payload to a frame previously created with
 * eth_create_frame().
 * ------------------------------------------------------------------------- */
#define eth_add_payload_nwu16_to_frame  enc28j60_write_nwu16_cont

/* -------------------------------------------------------------------------
 * Send an Ethernet frame, previously created with eth_create_frame().
 * total_nbr_of_bytes_in_payload:   number of bytes in payload
 *                                  (that is, excluding Ethernet header)
 * ------------------------------------------------------------------------- */
void
eth_send_frame(uint16_t total_nbr_of_bytes_in_payload);

/* -------------------------------------------------------------------------
 * Handle incoming frames, and invoke corresponding protocol handlers. If
 * the retransmission timer expires, re-send the last frame created with
 * 'retransmit_on_timeout' set.
 *
 * NOTE: this function will never return -- it will keep looping forever.
 * ------------------------------------------------------------------------- */
void
eth_handle_incoming_frames(void);

/* -------------------------------------------------------------------------
 * Retrieve received Ethernet payload.  Returns 16-bit checksum of retrieved
 * data, using _checksum_in as the initial value. 
 *
 * Assumes ERDPT points to the current reading location.
 * ------------------------------------------------------------------------- */
#define eth_retrieve_payload(_buf_ptr, _nbr_bytes, _checksum_in)              \
  enc28j60_read_memory_cont((const uint8_t *) (_buf_ptr),                     \
                            (_nbr_bytes),                                     \
                            (_checksum_in))

#endif /* SPECCYBOOT_ETH_INCLUSION_GUARD */
