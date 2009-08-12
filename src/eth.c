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

#include "rxbuffer.h"
#include "eth.h"

#include "enc28j60_spi.h"
#include "arp.h"
#include "ip.h"
#include "udp.h"
#include "util.h"
#include "syslog.h"

/* ========================================================================= */

const struct mac_address_t eth_broadcast_address = {
  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

const struct mac_address_t eth_local_address = {
  { MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2, MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5 }
};

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

#define TXBUF_FOR_CLASS(cl)     ( (cl == ETH_FRAME_PRIORITY)                  \
                                  ? (ENC28J60_TXBUF1_START)                   \
                                  : (ENC28J60_TXBUF2_START) )

/*
 * Position of next frame to read from the ENC28J60
 */
static enc28j60_addr_t next_frame = ENC28J60_RXBUF_START;

/*
 * Per-packet control byte: datasheet, section 7.1
 */
static const uint8_t per_packet_control_byte = 0x0E;

/* =========================================================================
 * RE-TRANSMISSION HANDLING
 *
 * When ack_timer >= retransmission_timeout, and no acknowledgment received:
 *   retransmit
 *   double retransmission_timeout, up to 24s
 * When acknowledgment received:
 *   reset retransmission_timeout to 3s
 * ========================================================================= */

static timer_t ack_timer;
static uint16_t retransmission_timeout;

/*
 * Special value for end_of_critical_frame to denote that no un-acknowledged
 * critical frame is in TXBUF1.
 */
#define NO_FRAME_NEEDS_RETRANSMISSION   (0)

/*
 * Value to write to ETXND when a re-transmission is to be performed
 */
static enc28j60_addr_t end_of_critical_frame = NO_FRAME_NEEDS_RETRANSMISSION;

#define RETRANSMISSION_TIMEOUT_MIN            (3 * (TICKS_PER_SECOND))
#define RETRANSMISSION_TIMEOUT_MAX            (24 * (TICKS_PER_SECOND))

#define ack_timer_expired()                                        \
  (timer_value(ack_timer) >= retransmission_timeout)

/* ------------------------------------------------------------------------- */

/*
 * An acknowledgment was recieved
 */
static void
ack_received(void)
{
  timer_reset(ack_timer);
  retransmission_timeout = RETRANSMISSION_TIMEOUT_MIN;
}

/* ------------------------------------------------------------------------- */

static void
setup_timer_for_retransmission(void)
{
  timer_reset(ack_timer);
  if (retransmission_timeout < RETRANSMISSION_TIMEOUT_MAX) {
    retransmission_timeout <<= 1;
  }
}

/* ------------------------------------------------------------------------- */

/*
 * Perform a frame transmission. Registers and ETXST and ETXND must be set
 * before this function is called.
 *
 * Does not return until the frame has been transmitted.
 *
 * start_address: address of the first byte in the frame
 * end_address:   address of the last byte in the frame
 */
static void
perform_transmission(enc28j60_addr_t start_address,
                     enc28j60_addr_t end_address)
{
  enc28j60_write_register16(ETXSTL, start_address);
  enc28j60_write_register16(ETXNDL, end_address);
  
  /*
   * Poll for link to come up (if it hasn't already)
   *
   * NOTE: this code assumes the MIREGADR/MICMD registers to be configured
   *       for continuous scanning of PHSTAT2 -- see eth_init()
   */
  enc28j60_poll_until_set(MIRDH, PHSTAT2_HI_LSTAT);

  /*
   * Errata, #10:
   *
   * Reset transmit logic before transmitting a frame
   */
  enc28j60_bitfield_set(ECON1, ECON1_TXRST);
  enc28j60_bitfield_clear(ECON1, ECON1_TXRST);
  
  enc28j60_bitfield_clear(EIE, EIE_TXIE);
  enc28j60_bitfield_clear(EIR, EIR_TXIF + EIR_TXERIF);

  enc28j60_bitfield_clear(ESTAT, ESTAT_TXABRT);

  enc28j60_bitfield_set(ECON1, ECON1_TXRTS);
  enc28j60_poll_until_clear(ECON1, ECON1_TXRTS);
  
  if ((enc28j60_read_register(ESTAT) & ESTAT_TXABRT) != 0) {
    uint16_t tsv;   /* transmit status vector, bits 16..31 */

    enc28j60_read_memory_at((uint8_t *) &tsv, end_address + 3, sizeof(tsv));
    syslog("TX fail tsv=%", tsv);
  }
}

/* ============================================================================
 * PUBLIC API
 * ========================================================================= */

void
eth_init(void)
{
  enc28j60_init();
  
  /*
   * ETH initialization
   *
   * Since the ENC28J60 doesn't support auto-negotiation, we will need to
   * stick to half duplex. Not a problem, since Ethernet performance is not
   * really a bottleneck on the Spectrum..
   */
  enc28j60_write_register16(ERXSTL, ENC28J60_RXBUF_START);
  enc28j60_write_register16(ERXNDL, ENC28J60_RXBUF_END);
  
  /* B5 errata, item 11: only odd values are allowed when writing ERXRDPT */
  enc28j60_write_register16(ERXRDPTL, ENC28J60_RXBUF_END);
  
  enc28j60_write_register(ERXFCON, ERXFCON_CRCEN);
  
  enc28j60_poll_until_set(ESTAT, ESTAT_CLKRDY);
  
  /*
   * MAC initialization: half duplex
   */
  enc28j60_write_register(MACON1, MACON1_MARXEN);
  /*
   * MACON3:
   * set bits PADCFG0..2 to pad all frames to at least 64B and append CRC
   */
  enc28j60_write_register(MACON3, 0xE0 + MACON3_TXCRCEN);
  enc28j60_write_register(MACON4, MACON4_DEFER);
  
  enc28j60_write_register16(MAMXFLL, MAX_FRAME_SIZE);
  
  enc28j60_write_register(MABBIPG, 0x12);    /* as per datasheet section 6.5 */
  enc28j60_write_register16(MAIPGL, 0x0C12); /* as per datasheet section 6.5 */
  
  enc28j60_write_register(MAADR1, MAC_ADDR_0);
  enc28j60_write_register(MAADR2, MAC_ADDR_1);
  enc28j60_write_register(MAADR3, MAC_ADDR_2);
  enc28j60_write_register(MAADR4, MAC_ADDR_3);
  enc28j60_write_register(MAADR5, MAC_ADDR_4);
  enc28j60_write_register(MAADR6, MAC_ADDR_5);
  
  /*
   * PHY initialization
   *
   * PHCON1 := 0x0100 -- half duplex
   */
  enc28j60_write_register(MIREGADR, PHCON1);
  enc28j60_write_register16(MIWRL, 0x0000);
  enc28j60_poll_until_clear(MISTAT, MISTAT_BUSY);
  
  /*
   * Set up PHY to automatically scan the PHSTAT2 every 10.24 us
   * (the current value can then be read directly from MIRD)
   *
   * NOTE: MIRD is not updated until 10.24us (37 T-states @3.5469MHz) after
   * this operation (datasheet, section 3.3.3). The delay below covers this
   * requirement too.
   */
  enc28j60_write_register(MIREGADR, PHSTAT2);
  enc28j60_bitfield_set(MICMD, MICMD_MIISCAN);
  
  /*
   * Enable reception and transmission
   */
  enc28j60_write_register(EIE, 0x00);    /* Disable all interrupts */
  enc28j60_write_register(EIR, 0x00);    /* No pending interrupts */
  enc28j60_write_register(ECON2, ECON2_AUTOINC);
  enc28j60_write_register(ECON1, ECON1_RXEN);
}

/* ------------------------------------------------------------------------- */

void
eth_create_frame(const struct mac_address_t *destination,
                 uint16_t                    ethertype,
                 enum eth_frame_class_t      frame_class)
{
  uint16_t tx_buf = TXBUF_FOR_CLASS(frame_class);
  
  enc28j60_write_register16(EWRPTL, tx_buf);
  
  enc28j60_write_memory_at(tx_buf,
                           &per_packet_control_byte,
                           sizeof(per_packet_control_byte));
  enc28j60_write_memory_cont(destination->addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_memory_cont(eth_local_address.addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_nwu16_cont(ethertype);
}

/* ------------------------------------------------------------------------- */

void
eth_add_payload_byte_to_frame(uint8_t b)
{
  enc28j60_write_memory_cont((const uint8_t *) &b, sizeof(b));
}

/* ------------------------------------------------------------------------- */

void
eth_send_frame(uint16_t                total_nbr_of_bytes_in_payload,
               enum eth_frame_class_t  frame_class)
{
  /*
   * Last address = start
   *              + 1 (per-packet control byte)
   *              + sizeof(struct eth_header_t)
   *              + nbr_bytes
   *              - 1 (point to last byte, not after it)
   *              = start + sizeof(struct eth_header_t) + nbr_bytes
   */
  uint16_t end_address = TXBUF_FOR_CLASS(frame_class)
                       + sizeof(struct eth_header_t)
                       + total_nbr_of_bytes_in_payload;
  
  if (frame_class == ETH_FRAME_PRIORITY) {
    end_of_critical_frame = end_address;
    
    /*
     * If we are sending a PRIORITY frame, it means that the previous one
     * has been acknowledged.
     */
    ack_received();
  }
  
  perform_transmission(TXBUF_FOR_CLASS(frame_class), end_address);
}

/* ------------------------------------------------------------------------- */

void
eth_handle_incoming_frames(void)
{
  uint16_t bytes_in_frame;                  /* size of current frame payload */
  
  ack_received();                          /* initial state: no ACK expected */
  
  for (;;) {
    /*
     * Spin here until at least one frame is received. Do this by
     * polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
     *
     * If the re-transmission timer expires, re-send the last frame
     * of class 'ETH_FRAME_PRIORITY', and reset the timer.
     */
    while(enc28j60_read_register(EPKTCNT) == 0) {
      if (ack_timer_expired()) {

        timer_reset(ack_timer);
        
        if (end_of_critical_frame != NO_FRAME_NEEDS_RETRANSMISSION) {
          
          /*
           * Timed out waiting for acknowledgment of a PRIORITY frame. Re-send
           * the last PRIORITY frame, and double the retransmission time-out.
           * Also add a bit of pseudo-random stuff to the time-out, because the
           * text-book says so. (Only matters if you have a large group of
           * SpeccyBoots on your LAN. Congratulations.)
           */
          
          if (retransmission_timeout >= RETRANSMISSION_TIMEOUT_MAX) {
            fatal_error("server not responding");
          }
          
          retransmission_timeout = retransmission_timeout
                                 + retransmission_timeout
                                 + rand5bits();
          
          perform_transmission(ENC28J60_TXBUF1_START, end_of_critical_frame);
        }
      }
    }

    /*
     * Parse the packet, pass it on to IP or ARP. Ignore the IP-style
     * checksum (because Ethernet uses its own FCS).
     */
    enc28j60_read_memory_at((uint8_t *) rx_eth_adm,
                            next_frame,
                            sizeof(rx_eth_adm));
    
    next_frame     = rx_eth_adm.next_ptr;
    bytes_in_frame = rx_eth_adm.nbr_bytes
                     - sizeof(struct eth_header_t)
                     - 4 /* Ethernet CRC */;

    enc28j60_bitfield_set(ECON2, ECON2_PKTDEC);         /* decrease EPKTCNT */
    
    /*
     * Filter out broadcasts from this host
     */
    if (   rx_eth_adm.eth_header.src_addr.addr[0] != MAC_ADDR_0
        || rx_eth_adm.eth_header.src_addr.addr[1] != MAC_ADDR_1
        || rx_eth_adm.eth_header.src_addr.addr[2] != MAC_ADDR_2
        || rx_eth_adm.eth_header.src_addr.addr[3] != MAC_ADDR_3
        || rx_eth_adm.eth_header.src_addr.addr[4] != MAC_ADDR_4
        || rx_eth_adm.eth_header.src_addr.addr[5] != MAC_ADDR_5)
    {
      switch (rx_eth_adm.eth_header.ethertype) {
        case htons(ETHERTYPE_IP):
          ip_frame_received(bytes_in_frame);
          break;
        case htons(ETHERTYPE_ARP):
          arp_frame_received(bytes_in_frame);
          break;
      }
    }
    
    /*
     * Advance ERXRDPT
     * 
     * Datasheet, #6.1: EXRDPT is written in order ERXRDPTL, ERXRDPTH
     * Errata B5, #11:  EXRDPT must always be written with an odd value
     *
     * (next_frame itself is always even -- padded by ENC28J60 if necessary)
     */    
    
    if (next_frame > ENC28J60_RXBUF_END) {
      fatal_error("RX ptr out of range");
    }
    enc28j60_write_register16(ERXRDPTL, next_frame - 1);
  }
}
