/*
 * Module eth:
 *
 * Ethernet implementation using the Microchip ENC28J60 Ethernet host.
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

#include "eth.h"

#include "arp.h"
#include "globals.h"
#include "syslog.h"
#include "udp_ip.h"
#include "ui.h"

/* ========================================================================= */

const struct mac_address_t eth_broadcast_address = {
  { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

const struct mac_address_t eth_local_address = {
  { MAC_ADDR_0, MAC_ADDR_1, MAC_ADDR_2, MAC_ADDR_3, MAC_ADDR_4, MAC_ADDR_5 }
};

/* Position of next frame to read from the ENC28J60 */
static enc28j60_addr_t next_frame;

/* Per-packet control byte: datasheet, section 7.1 */
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

static uint16_t retransmission_timeout;

/*
 * Special value for end_of_critical_frame to denote that no un-acknowledged
 * critical frame is in TXBUF1.
 */
#define NO_FRAME_NEEDS_RETRANSMISSION         (0)

/* Value to write to ETXND when a re-transmission is to be performed */
static enc28j60_addr_t end_of_critical_frame;

#define RETRANSMISSION_TIMEOUT_MIN            (3 * (TICKS_PER_SECOND))
#define RETRANSMISSION_TIMEOUT_MAX            (24 * (TICKS_PER_SECOND))

#define ack_timer_expired()                                                   \
  (timer_value(ack_timer) > retransmission_timeout)

/* ------------------------------------------------------------------------- */

/* Timer for re-transmissions */
static timer_t ack_timer;

/* ------------------------------------------------------------------------- */

/* Points to start of the buffer for the frame currently being created */
static enc28j60_addr_t current_txbuf;

/* ========================================================================= */

/* End of table below */
#define END_OF_TABLE                         (ENC28J60_UNUSED_REG)

/* Initial ENC28J60 register values */
static const uint8_t eth_register_values[] = {
  /*
   * ETH initialization
   *
   * Since the ENC28J60 doesn't support auto-negotiation, we will need to
   * stick to half duplex. Not a problem, since Ethernet performance is not
   * really a bottleneck on the Spectrum.
   */
  ERXSTL,   LOBYTE(ENC28J60_RXBUF_START),
  ERXSTH,   HIBYTE(ENC28J60_RXBUF_START),

  ERXNDL,   LOBYTE(ENC28J60_RXBUF_END),
  ERXNDH,   HIBYTE(ENC28J60_RXBUF_END),

  /* B5 errata, item 11: only odd values are allowed when writing ERXRDPT */
  ERXRDPTL, LOBYTE(ENC28J60_RXBUF_END),
  ERXRDPTH, HIBYTE(ENC28J60_RXBUF_END),

  ERXFCON,  ERXFCON_CRCEN,

  /*
   * MAC initialization: half duplex
   */
  MACON1,   MACON1_MARXEN,

  /* MACON3: set bits PADCFG0..2 to pad frames to at least 64B, append CRC */
  MACON3,   (uint8_t) (0xE0 + MACON3_TXCRCEN),
  MACON4,   MACON4_DEFER,

  MAMXFLL,  LOBYTE(ETH_MAX_RX_FRAME_SIZE),
  MAMXFLH,  HIBYTE(ETH_MAX_RX_FRAME_SIZE),

  MABBIPG,  0x12,    /* as per datasheet section 6.5 */
  MAIPGL,   0x12,    /* as per datasheet section 6.5 */
  MAIPGH,   0x0C,    /* as per datasheet section 6.5 */

  MAADR1,   MAC_ADDR_0,
  MAADR2,   MAC_ADDR_1,
  MAADR3,   MAC_ADDR_2,
  MAADR4,   MAC_ADDR_3,
  MAADR5,   MAC_ADDR_4,
  MAADR6,   MAC_ADDR_5,

  /*
   * PHY initialization
   */
  MIREGADR, PHCON1,
  MIWRL,    0x00,   /* PHCON1 := 0x0000 -- half duplex */
  MIWRH,    0x00,

  /*
   * Set up PHY to automatically scan the PHSTAT2 every 10.24 us
   * (the current value can then be read directly from MIRD)
   */
  MIREGADR, PHSTAT2,
  MICMD,    MICMD_MIISCAN,

  /* Enable reception and transmission */
  EIE,      0x00,    /* disable all interrupts */
  EIR,      0x00,    /* no pending interrupts */
  ECON2,    ECON2_AUTOINC,
  ECON1,    ECON1_RXEN,

  END_OF_TABLE
};

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
  enc28j60_select_bank(BANK(ETXSTL));
  enc28j60_write_register16(ETXST, start_address);
  enc28j60_write_register16(ETXND, end_address);

  /*
   * Poll for link to come up (if it hasn't already)
   *
   * NOTE: this code assumes the MIREGADR/MICMD registers to be configured
   *       for continuous scanning of PHSTAT2 -- see eth_init()
   */
  enc28j60_select_bank(BANK(MIRDH));
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

  enc28j60_select_bank(ENC28J60_DEFAULT_BANK);
}

/* ============================================================================
 * PUBLIC API
 * ========================================================================= */

void
eth_init(void)
{
  uint8_t reg;
  const uint8_t *params = eth_register_values;

  enc28j60_init();
  enc28j60_poll_until_set(ESTAT, ESTAT_CLKRDY);

  /*
   * Set up registers using the table above. PHY register access requires a
   * delay of at least 10.24us (approx 37 T-states) between writes. The
   * overhead in this loop safely covers this.
   */
  while ((reg = *params++) != END_OF_TABLE) {
    enc28j60_select_bank(BANK(reg));
    enc28j60_write_register(reg, *params++);
  }

  next_frame = ENC28J60_RXBUF_START;
}

/* ------------------------------------------------------------------------- */

void
eth_create(const struct mac_address_t *destination,
           uint16_t                    ethertype,
           eth_frame_class_t           frame_class)
{
  current_txbuf = frame_class;                  /* Maps directly to a buffer */

  enc28j60_select_bank(ENC28J60_DEFAULT_BANK);
  enc28j60_write_memory_at(current_txbuf,
                           &per_packet_control_byte,
                           sizeof(per_packet_control_byte));
  enc28j60_write_memory_cont(destination->addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_memory_cont(eth_local_address.addr,
                             sizeof(struct mac_address_t));
  enc28j60_write_memory_cont((const uint8_t *) &ethertype, sizeof(ethertype));
}

/* ------------------------------------------------------------------------- */

void
eth_send(uint16_t total_nbr_of_bytes_in_payload)
{
  /*
   * Last address = start
   *              + 1 (per-packet control byte)
   *              + sizeof(struct eth_header_t)
   *              + nbr_bytes
   *              - 1 (point to last byte, not after it)
   *              = start + sizeof(struct eth_header_t) + nbr_bytes
   */
  uint16_t end_address = current_txbuf
                       + sizeof(struct eth_header_t)
                       + total_nbr_of_bytes_in_payload;

  if (current_txbuf == ETH_FRAME_PRIORITY) {
    end_of_critical_frame = end_address;

    /* Sending a PRIORITY frame implies the previous one was acknowledged. */
    timer_reset(ack_timer);
    retransmission_timeout = RETRANSMISSION_TIMEOUT_MIN;
  }

  perform_transmission(current_txbuf, end_address);
}

/* -------------------------------------------------------------------------
 * Main function: initiate DHCP, receive frames and act on them
 * ------------------------------------------------------------------------- */

void
main(void)
__naked
{
#ifdef SB_MINIMAL

  __asm

    xor a
    out (0xfe), a
    ld  hl, #0x4000
    ld  de, #0x4001
    ld  bc, #0x1AFF
    ld  (hl), a
    ldir

  __endasm;

  eth_init();
  bootp_init();
#else
  cls();

  eth_init();
  dhcp_init();
#endif

  /*
   * At this point, eth_send() has already been called once for a PRIORITY
   * frame (DHCP above), so the ACK timer does not need to be reset.
   */

  for (;;) {
    /*
     * Spin here until at least one frame is received. Do this by
     * polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
     *
     * If the re-transmission timer expires, re-send the last frame
     * of class 'ETH_FRAME_PRIORITY', and reset the timer.
     */
    for (;;) {

      enc28j60_select_bank(BANK(EPKTCNT));
      if (enc28j60_read_register(EPKTCNT)) {
        break;
      }

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
            fatal_error(FATAL_NO_RESPONSE);
          }

          __asm

            ld    hl, (_retransmission_timeout)
            add   hl, hl

            ld    a, r
            add   a, #MAC_ADDR_0+MAC_ADDR_1+MAC_ADDR_2+MAC_ADDR_3+MAC_ADDR_4+MAC_ADDR_5
            and   a, #0x1f
            add   a, l
            jr    nc, 9999$
            inc   h
          9999$:
            ld    l, a
            ld    (_retransmission_timeout), hl

          __endasm;

          syslog("re-sending");
          perform_transmission(ENC28J60_TXBUF1_START, end_of_critical_frame);
        }
      }
    }

    enc28j60_select_bank(ENC28J60_DEFAULT_BANK);

    /* Parse the packet, pass it on to IP or ARP. */
    enc28j60_read_memory_at((uint8_t *) rx_eth_adm,
                            next_frame,
                            sizeof(rx_eth_adm));

    next_frame = rx_eth_adm.next_ptr;
    if (next_frame > ENC28J60_RXBUF_END) {  /* sanity check */
      eth_init();
      syslog("ETH reset");
      continue;
    }

    enc28j60_bitfield_set(ECON2, ECON2_PKTDEC);         /* decrease EPKTCNT */

    /* Filter out broadcasts from this host. */
    if (   rx_eth_adm.eth_header.src_addr.addr[0] != MAC_ADDR_0
        || rx_eth_adm.eth_header.src_addr.addr[1] != MAC_ADDR_1
        || rx_eth_adm.eth_header.src_addr.addr[2] != MAC_ADDR_2
        || rx_eth_adm.eth_header.src_addr.addr[3] != MAC_ADDR_3
        || rx_eth_adm.eth_header.src_addr.addr[4] != MAC_ADDR_4
        || rx_eth_adm.eth_header.src_addr.addr[5] != MAC_ADDR_5)
    {
      switch (rx_eth_adm.eth_header.ethertype) {
        case htons(ETHERTYPE_IP):
          ip_receive();
          break;
        case htons(ETHERTYPE_ARP):
          arp_receive();
          break;
      }
    }

    /*
     * Advance ERXRDPT
     *
     * Errata B5, #11:  EXRDPT must always be written with an odd value
     */
    enc28j60_select_bank(BANK(ERXRDPTL));
    enc28j60_write_register16(ERXRDPT, (next_frame - 1) | 1);
  }
}
