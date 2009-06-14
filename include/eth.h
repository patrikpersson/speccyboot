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

#include "speccyboot.h"
#include "enc28j60_spi.h"

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
 * Ethernet HW type (as used by, e.g., ARP)
 */
#define ETH_HWTYPE                  (1)

/*
 * Two classes of frames:
 *
 * PRIORITY       Automatically re-transmitted when a timer expires. If
 *                another PRIORITY frame is transmitted, the timer is reset.
 *
 *                This means frames where we care about an answer.
 *
 * OPTIONAL       Not automatically re-transmitted. The timer is not affected
 *                in any way. The frame may be silently dropped if the
 *                on-chip storage is in use for other stuff -- see
 *                eth_store_data().
 *
 *                This means frames where we do NOT care about an answer.
 *
 * NOTE: the frame class determines the choice of transmission buffer.
 */
enum eth_frame_class_t {
  ETH_FRAME_PRIORITY    = 1,
  ETH_FRAME_OPTIONAL    = 2
};
 
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
                 enum eth_frame_class_t      frame_class);

/* -------------------------------------------------------------------------
 * Append payload to a frame previously created with eth_create_frame().
 * ------------------------------------------------------------------------- */
void
eth_add_payload_to_frame(const void *payload,
                         uint16_t    nbr_bytes);

/* -------------------------------------------------------------------------
 * Re-write outgoing packet
 *
 * NOTE: This call modifies the internal write pointer, so any calls to
 *       eth_add_payload_to_frame() will make Bad Things happen. That is,
 *       call after eth_add_payload_to_frame(), before eth_send_frame().
 * ------------------------------------------------------------------------- */
void
eth_rewrite_frame(uint16_t                offset,
                  const void             *payload,
                  uint16_t                nbr_bytes,
                  enum eth_frame_class_t  frame_class);

/* -------------------------------------------------------------------------
 * Send an Ethernet frame, previously created with eth_create_frame().
 * total_nbr_of_bytes_in_payload:   number of bytes in payload
 *                                  (that is, excluding Ethernet header)
 * frame_class:                     MUST be the same as used for
 *                                  eth_create_frame(), or Bad Things will
 *                                  happen
 * ------------------------------------------------------------------------- */
void
eth_send_frame(uint16_t                total_nbr_of_bytes_in_payload,
               enum eth_frame_class_t  frame_class);

/* -------------------------------------------------------------------------
 * Reset re-transmission timer. If this is not called, the last frame
 * created with 'retransmit_on_timeout' set will be re-transmitted when the
 * timer expires.
 * ------------------------------------------------------------------------- */
void
eth_reset_retransmission_timer(void);

/* -------------------------------------------------------------------------
 * Handle incoming frames, and invoke corresponding protocol handlers. If
 * the retransmission timer expires, re-send the last frame created with
 * 'retransmit_on_timeout' set.
 *
 * NOTE: this function will never return -- it will keep looping forever.
 * ------------------------------------------------------------------------- */
void
eth_handle_incoming_frames(void);

#endif /* SPECCYBOOT_ETH_INCLUSION_GUARD */
