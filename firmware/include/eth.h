/*
 * Module eth:
 *
 * Ethernet implementation using the Microchip ENC28J60 Ethernet host.
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
#ifndef SPECCYBOOT_ETH_INCLUSION_GUARD
#define SPECCYBOOT_ETH_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

#include "enc28j60.h"
#include "util.h"

/* -------------------------------------------------------------------------
 * ENC28J60 buffer management
 * ------------------------------------------------------------------------- */

/* Worst-case payload for transmitted frames (DHCP REQUEST) */
#define ETH_MAX_TX_PAYLOAD        (300)

/*
 * Worst-case payload for received frames:
 *  60   (max IP header size)
 *   8   (UDP header)
 * 576   (maximal accepted DHCP packet size; minimal value allowed by RFC)
 * ---
 * 644 bytes
 */
#define ETH_MAX_RX_FRAME_SIZE      (sizeof(struct eth_header_t) + 644)

/*
 * Transmission buffer size:
 * Ethernet header, payload, and 8 bytes of administrative info stored
 * by controller
 */
#define ENC28J60_TXBUF_SIZE                                                   \
  (sizeof(struct eth_header_t) + ETH_MAX_TX_PAYLOAD + 8)

/*
 * MEMORY MAP
 * ==========
 *
 * Errata for silicon rev. B5, item #3: receive buffer must start at 0x0000
 *
 * 0x0000   ... 0xXXXX  Receive buffer (FIFO, about 5.5K): automatically
 *                      filled with received packets by the ENC28J60. The host
 *                      updates ERXRDPT to inform the ENC28J60 when data is
 *                      consumed.
 *
 * 0xXXXX+1 ... 0xYYYY  TX buffer 1. This is the important one -- used for
 *                      frame class CRITICAL. On time-outs, this frame will be
 *                      re-transmitted (if valid).
 *
 * 0xYYYY+1 ... 0x17FF  TX buffer 2. This buffer is used for frames where no
 *                      reply is expected -- frame class OPTIONAL.
 *
 * 0x1800   ... 0x1FFF  Reserved for temporary storage during snapshot
 *                      loading (see context_switch.c)
 *
 * Also see the comment for eth_frame_class_t (eth.h).
 */

#define ENC28J60_RXBUF_START    (0x0000)
#define ENC28J60_EVACUATED_DATA (0x1800)

#define ENC28J60_TXBUF2_START   (ENC28J60_EVACUATED_DATA - ENC28J60_TXBUF_SIZE)
#define ENC28J60_TXBUF1_START   (ENC28J60_TXBUF2_START   - ENC28J60_TXBUF_SIZE)
#define ENC28J60_RXBUF_END      (ENC28J60_TXBUF1_START - 1)

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
   */
  PACKED_STRUCT(eth_header_t) {
    struct mac_address_t  dst_addr;
    struct mac_address_t  src_addr;
    uint16_t              ethertype;
  } eth_header;
};

/* ========================================================================= */

/* Ethernet HW type (as used by, e.g., ARP) */
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
 * Disable Ethernet (pull controller into reset, and keep it there)
 * ------------------------------------------------------------------------- */
#define eth_disable enc28j60_disable

/* -------------------------------------------------------------------------
 * Create an Ethernet frame for transmission. If a previous frame is
 * currently in transmission, block until that is done first.
 *
 * destination:           destination MAC address, or eth_broadcast_address
 * ethertype:             value for length/type field
 * frame_class:           see comment for enum eth_frame_class_t above
 * ------------------------------------------------------------------------- */
void
eth_create(const struct mac_address_t *destination,
           uint16_t                    ethertype,
           eth_frame_class_t           frame_class);

/* -------------------------------------------------------------------------
 * Append payload to a frame previously created with eth_create().
 * ------------------------------------------------------------------------- */
#define eth_add_w_len(_pld, _n)                                               \
  enc28j60_write_memory_cont((const uint8_t *) (_pld), (_n))

#define eth_add(_pld)                                                         \
   eth_add_w_len(&(_pld), sizeof(_pld))

/* -------------------------------------------------------------------------
 * Send an Ethernet frame, previously created with eth_create().
 * total_nbr_of_bytes_in_payload:   number of bytes in payload
 *                                  (that is, excluding Ethernet header)
 * ------------------------------------------------------------------------- */
void
eth_send(uint16_t total_nbr_of_bytes_in_payload);

/* -------------------------------------------------------------------------
 * Retrieve received Ethernet payload.  Returns 16-bit checksum of retrieved
 * data, using _checksum_in as the initial value. 
 *
 * Assumes ERDPT points to the current reading location.
 * ------------------------------------------------------------------------- */
#define eth_retrieve_payload(_buf_ptr, _nbr_bytes)                            \
  enc28j60_read_memory_cont((const uint8_t *) (_buf_ptr), (_nbr_bytes))

#endif /* SPECCYBOOT_ETH_INCLUSION_GUARD */
