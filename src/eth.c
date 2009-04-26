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

#include <stddef.h>

#include "eth.h"

#include "enc28j60_spi.h"
#include "arp.h"
#include "ip.h"
#include "spectrum.h"
#include "speccyboot.h"
#include "logging.h"

/* ========================================================================= */

/*
 * Ethernet header
 * http://en.wikipedia.org/wiki/Ethernet
 */
PACKED_STRUCT(eth_header_t) {
  struct mac_address_t  dst_addr;
  struct mac_address_t  src_addr;
  uint16_t              ethertype;
};

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
 * Size of a transmission buffer: administrative data, Ethernet header,
 *                                payload, padded up to an even number of bytes
 */
#define guckENC28J60_TXBUF1_SIZE    ((MAX_TX_FRAME_SIZE + ENC28J60_TX_ADM + 1) \
                                     & 0xfffe)

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
 * 0xXXXX+1...0x1FFF    TX buffer 2/scratchpad. This buffer is used for frames
 *                      where no reply is expected -- frame class OPTIONAL.
 *
 * Also see the comment for eth_frame_class_t (eth.h).
 */

#define ENC28J60_RXBUF_START    (0x0000)
#define ENC28J60_RXBUF_END      (0x0FFF)
#define ENC28J60_TXBUF1_START   (0x1000)
#define ENC28J60_TXBUF1_END     (ENC28J60_TXBUF1_START + ENC28J60_TXBUF_SIZE-1)
#define ENC28J60_TXBUF2_START   (ENC28J60_TXBUF1_START + ENC28J60_TXBUF_SIZE)
#define ENC28J60_TXBUF2_END     (0x1FFF)

#define TXBUF_FOR_CLASS(cl)     ( (cl == ETH_FRAME_PRIORITY)                  \
                                  ? (ENC28J60_TXBUF1_START)                   \
                                  : (ENC28J60_TXBUF2_START) )

#define TXBUFSZ_FOR_CLASS(cl)   ( (cl == ETH_FRAME_PRIORITY)                  \
                                  ? (ENC28J60_TXBUF1_SIZE)                    \
                                  : (ENC28J60_TXBUF2_SIZE) )

/* =========================================================================
 * HOST-SIDE RX BUFFERS
 * ========================================================================= */

/*
 * Ethernet and administrative data, as written by ENC28J60 reception logic
 * (datasheet, 7.2.2)
 */
PACKED_STRUCT(rx_header_buf_t) {
  enc28j60_addr_t       next_ptr;    /* written as little-endian by ENC28J60 */
  uint16_t              nbr_bytes;   /* written as little-endian by ENC28J60 */
  uint8_t               rsv16to23;
  uint8_t               rsv24to31;
  
  struct eth_header_t   eth_header;
};

static struct rx_header_buf_t rx_header_buf;
static                uint8_t rx_frame_buf[ENC28J60_RXBUF_SIZE];

/* =========================================================================
 * RE-TRANSMISSION HANDLING
 * ========================================================================= */

#define TICKS_PER_SECOND                (50)
#define RETRANSMISSION_TIMEOUT          (4 * (TICKS_PER_SECOND))

#define retransmission_timer_expired()                                        \
  (timer_tick_count >= RETRANSMISSION_TIMEOUT)

/*
 * Special value for end_of_critical_frame to denote that no un-acknowledged
 * critical frame is in TXBUF1.
 */
#define NO_FRAME_NEEDS_RETRANSMISSION   (0)

/*
 * Tick count, increased by the 50Hz timer ISR in crt0.asm
 */
volatile uint8_t timer_tick_count = 0;

/*
 * Value to write to ETXND when a re-transmission is to be performed
 */
static enc28j60_addr_t end_of_critical_frame = NO_FRAME_NEEDS_RETRANSMISSION;

/* ------------------------------------------------------------------------- */

/*
 * Perform a frame transmission. Registers and ETXST and ETXND must be set
 * before this function is called.
 *
 * Does not return until the frame has been transmitted.
 */
static void
perform_transmission(void)
{  
  /*
   * Poll for link to come up (if it hasn't already)
   *
   * NOTE: this code assumes the MIREGADR/MICMD registers to be configured
   *       for continuous scanning of PHSTAT2 -- see eth_init()
   */
#if 0
  {
    int i;
    for (i = 0; i < 50; i++) {
      uint8_t v = enc28j60_read_register(MIRDH);
      logging_add_entry("ETH: PHSTAT2H=" HEX8_ARG, &v);
    }
  }
#endif
  // enc28j60_poll_until_set(MIRDH, PHSTAT2_HI_LSTAT);
  // logging_add_entry("ETH: transmitting", NULL);
  
  enc28j60_bitfield_set(ECON1, ECON1_TXRTS);
  enc28j60_poll_until_clear(ECON1, ECON1_TXRTS);
  
  if ((enc28j60_read_register(ESTAT) & ESTAT_TXABRT) != 0) {
    uint8_t estat = enc28j60_read_register(ESTAT);
    
    logging_add_entry("ETH: send fail ESTAT=" HEX8_ARG, &estat);
    
    fatal_error("transmission failed");
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
   */
  enc28j60_write_register(ERXSTL, LOBYTE(ENC28J60_RXBUF_START));
  enc28j60_write_register(ERXSTH, HIBYTE(ENC28J60_RXBUF_START));
  enc28j60_write_register(ERXNDL, LOBYTE(ENC28J60_RXBUF_END));
  enc28j60_write_register(ERXNDH, HIBYTE(ENC28J60_RXBUF_END));
  
  /* B5 errata, item 11: only odd values are allowed when writing ERXRDPT */
  enc28j60_write_register(ERXRDPTL, LOBYTE(ENC28J60_RXBUF_END));
  enc28j60_write_register(ERXRDPTH, HIBYTE(ENC28J60_RXBUF_END));

  enc28j60_write_register(ERXFCON, ERXFCON_CRCEN);
  
  enc28j60_poll_until_set(ESTAT, ESTAT_CLKRDY);
  
  /*
   * MAC initialization: full duplex
   */
  enc28j60_write_register(MACON1,
                          MACON1_MARXEN + MACON1_RXPAUS + MACON1_TXPAUS);
  /*
   * MACON3:
   * set bits PADCFG0..2 to pad all frames to at least 64B and append CRC
   */
  enc28j60_write_register(MACON3, 0xE0 + MACON3_TXCRCEN + MACON3_FULDPX);
  enc28j60_write_register(MACON4, MACON4_DEFER);
  
  enc28j60_write_register(MABBIPG, 0x15);    /* as per datasheet section 6.5 */
  enc28j60_write_register(MAIPGL,  0x12);    /* as per datasheet section 6.5 */
  // enc28j60_write_register(MAIPGH,  0x0C);    /* as per datasheet section 6.5 */
  
  // enc28j60_write_register(MACLCON1, 0x0F);   /* default (reset) value */
  // enc28j60_write_register(MACLCON2, 0x37);   /* default (reset) value */
  
  enc28j60_write_register(MAMXFLL, LOBYTE(MAX_FRAME_SIZE));
  enc28j60_write_register(MAMXFLH, HIBYTE(MAX_FRAME_SIZE));
  
  enc28j60_write_register(MAADR1, MAC_ADDR_0);
  enc28j60_write_register(MAADR2, MAC_ADDR_1);
  enc28j60_write_register(MAADR3, MAC_ADDR_2);
  enc28j60_write_register(MAADR4, MAC_ADDR_3);
  enc28j60_write_register(MAADR5, MAC_ADDR_4);
  enc28j60_write_register(MAADR6, MAC_ADDR_5);
  
  /*
   * PHY initialization
   *
   * PHCON1 := 0x0100 -- full duplex
   */
  enc28j60_write_register(MIREGADR, PHCON1);
  enc28j60_write_register(MIWRL, LOBYTE(0x0100));
  enc28j60_write_register(MIWRH, HIBYTE(0x0100));
  enc28j60_poll_until_clear(MISTAT, MISTAT_BUSY);

  /*
   * Set up PHY to automatically scan the PHSTAT2 every 10.24 us
   * (the current value can then be read directly from MIRD)
   *
   * NOTE: MIRD is not updated until 10.24us (37 T-states @3.5469MHz) after
   * this operation (datasheet, section 3.3.3). The ETH operations below
   * provide sufficient delay to guarantee that MIRD makes sense afterwards.
   */
  enc28j60_write_register(MIREGADR, PHSTAT2);
  enc28j60_bitfield_set(MICMD, MICMD_MIISCAN);
  __asm
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  nop
  __endasm;
  
  /*
   * Enable reception and transmission
   */
  enc28j60_write_register(EIE, 0x00);    /* Disable all interrupts */
  enc28j60_write_register(EIR, 0x00);    /* No pending interrupts */
  enc28j60_write_register(ECON2, ECON2_AUTOINC);
  enc28j60_write_register(ECON1, ECON1_CSUMEN + ECON1_RXEN);
}

/* ------------------------------------------------------------------------- */

void
eth_create_frame(const struct mac_address_t *destination,
                 uint16_t                    ethertype,
                 enum eth_frame_class_t      frame_class)
{

  /*
   * Datasheet, section 7.1
   */
  static const uint8_t per_packet_control_byte = 0x0E;
  
  uint16_t ethertype_in_nw_order = htons(ethertype);
  uint16_t                tx_buf = TXBUF_FOR_CLASS(frame_class);

  if (frame_class == ETH_FRAME_PRIORITY) {
    eth_reset_retransmission_timer();
  }
  
  enc28j60_write_register(ETXSTH, HIBYTE(tx_buf));
  enc28j60_write_register(ETXSTL, LOBYTE(tx_buf));
  
  enc28j60_write_register(EWRPTH, HIBYTE(tx_buf));
  enc28j60_write_register(EWRPTL, LOBYTE(tx_buf));

  enc28j60_write_memory_at(tx_buf,
                           &per_packet_control_byte,
                           sizeof(per_packet_control_byte));
  enc28j60_write_memory_cont(destination->addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_memory_cont(eth_local_address.addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_memory_cont((uint8_t *) &ethertype_in_nw_order,
                             sizeof(uint16_t));
}

/* ------------------------------------------------------------------------- */

void
eth_add_payload_to_frame(const void *payload,
                         uint16_t    nbr_bytes)
{
  enc28j60_write_memory_cont((const uint8_t *) payload, nbr_bytes);
}

/* ------------------------------------------------------------------------- */

void
eth_rewrite_frame(uint16_t                offset,
                  const void             *payload,
                  uint16_t                nbr_bytes,
                  enum eth_frame_class_t  frame_class)
{
  enc28j60_write_memory_at(TXBUF_FOR_CLASS(frame_class)
                            + sizeof(struct eth_header_t)
                            + 1   /* per-packet control byte */
                            + offset,
                           payload,
                           nbr_bytes);
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
  }
  
  enc28j60_write_register(ETXNDL, LOBYTE(end_address));
  enc28j60_write_register(ETXNDH, HIBYTE(end_address));

  perform_transmission();
}

/* ------------------------------------------------------------------------- */

void
eth_outgoing_ip_checksum(uint16_t                start_offset,
                         uint16_t                number_of_bytes_to_checksum,
                         uint16_t                checksum_offset,
                         enum eth_frame_class_t  frame_class)
{
  uint8_t checksum[2];
  uint16_t payload_addr  = TXBUF_FOR_CLASS(frame_class)
                           + 1  /* per-packet control byte */
                           + sizeof(struct eth_header_t);
  uint16_t start_addr    = payload_addr + start_offset;
  uint16_t checksum_addr = payload_addr + checksum_offset;
  uint16_t end_addr      = start_addr   + number_of_bytes_to_checksum - 1;
  
  enc28j60_write_register(EDMASTL, LOBYTE(start_addr));
  enc28j60_write_register(EDMASTH, HIBYTE(start_addr));
  
  enc28j60_write_register(EDMANDL, LOBYTE(end_addr));
  enc28j60_write_register(EDMANDH, HIBYTE(end_addr));
  
  enc28j60_bitfield_set(ECON1, ECON1_DMAST);
  enc28j60_poll_until_clear(ECON1, ECON1_DMAST);
  
  /* big-endian checksum (apparently) */
  checksum[0] = enc28j60_read_register(EDMACSH);
  checksum[1] = enc28j60_read_register(EDMACSL);

  enc28j60_write_memory_at(checksum_addr, checksum, sizeof(uint16_t));
}

/* ------------------------------------------------------------------------- */

void
eth_reset_retransmission_timer(void)
{
  timer_tick_count = 0; 
  // end_of_critical_frame = NO_FRAME_NEEDS_RETRANSMISSION;
}

/* ------------------------------------------------------------------------- */

void
eth_handle_incoming_frames(void)
{
  enc28j60_addr_t rx_ptr = ENC28J60_RXBUF_START;
  
  for (;;) {
    /*
     * Spin here until at least one frame is received. Do this by
     * polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
     *
     * If the re-transmission timer expires, re-send the last frame
     * with 'retransmit_on_timeout' flag set, and reset the timer.
     */
    while(enc28j60_read_register(EPKTCNT) == 0) {
      if (retransmission_timer_expired()) {
        eth_reset_retransmission_timer();
        
        if (end_of_critical_frame != NO_FRAME_NEEDS_RETRANSMISSION) {

          enc28j60_write_register(ETXSTH, HIBYTE(ENC28J60_TXBUF1_START));
          enc28j60_write_register(ETXSTL, LOBYTE(ENC28J60_TXBUF1_START));
          
          enc28j60_write_register(ETXNDL, LOBYTE(end_of_critical_frame));
          enc28j60_write_register(ETXNDH, HIBYTE(end_of_critical_frame));

          perform_transmission();
          
          // logging_add_entry("ETH: retransmitted!", NULL);
        }
      }
    }

    /*
     * Read ENC28J60 frame status info + Ethernet header
     */
    enc28j60_read_memory((uint8_t *) rx_header_buf,
                         rx_ptr,
                         sizeof(rx_header_buf));
    
    {
      uint16_t bytes_in_frame = rx_header_buf.nbr_bytes
                                - sizeof(struct eth_header_t)
                                - 4 /* Ethernet CRC */;
      
      if (bytes_in_frame <= sizeof(rx_frame_buf)) {   /* paranoia */
        enc28j60_read_memory(rx_frame_buf,
                             rx_ptr + sizeof(rx_header_buf),
                             bytes_in_frame);
        
        switch (rx_header_buf.eth_header.ethertype) {
          case htons(ETHERTYPE_IP):
            ip_frame_received(&rx_header_buf.eth_header.src_addr,
                              rx_frame_buf,
                              bytes_in_frame);
            break;
          case htons(ETHERTYPE_ARP):
            arp_frame_received(&rx_header_buf.eth_header.src_addr,
                               rx_frame_buf,
                               bytes_in_frame);
            break;
        }
      }
    }
    
    /*
     * Advance ERXRDPT
     * 
     * Datasheet, #6.1: EXRDPT is written in order ERXRDPTL, ERXRDPTH
     * Errata B5, #11:  EXRDPT must always be written with an odd value
     *
     * (rx_ptr itself is always even -- padded by ENC28J60 if necessary)
     */
    rx_ptr = rx_header_buf.next_ptr;
    
    if (rx_ptr > ENC28J60_RXBUF_END) {
      fatal_error("rx_ptr out of range");
    }

    enc28j60_write_register(ERXRDPTL, LOBYTE(rx_ptr - 1));
    enc28j60_write_register(ERXRDPTH, HIBYTE(rx_ptr - 1));

    /*
     * Decrease EPKTCNT
     */
    enc28j60_bitfield_set(ECON2, ECON2_PKTDEC);
  }
}
