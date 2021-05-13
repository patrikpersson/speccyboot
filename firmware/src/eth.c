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
#include "bootp.h"
#include "globals.h"
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

/* =========================================================================
 * RE-TRANSMISSION HANDLING
 *
 * When ack_timer >= retransmission_timeout, and no acknowledgment received:
 *   retransmit
 *   double retransmission_timeout, up to 20.48s
 * When acknowledgment received:
 *   reset retransmission_timeout to 2.56s
 * ========================================================================= */

static uint8_t retransmission_timeout;  /* high byte of time-out */

/*
 * Special value for end_of_critical_frame to denote that no un-acknowledged
 * critical frame is in TXBUF1.
 */
#define NO_FRAME_NEEDS_RETRANSMISSION         (0)

/* Value to write to ETXND when a re-transmission is to be performed */
static enc28j60_addr_t end_of_critical_frame;

/* Timeouts for packet re-transmission, high byte only */
#define RETRANSMISSION_TIMEOUT_MIN            (0x01)
#define RETRANSMISSION_TIMEOUT_MAX            (0x08)

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

/* ============================================================================
 * PUBLIC API
 * ========================================================================= */

void
eth_init(void)
__naked
{
  __asm

    ;; ========================================================================
    ;; reset Ethernet controller
    ;;
    ;; Data sheet, Table 16.3: Trstlow = 400ns
    ;; (minimal RST low time, shorter pulses are filtered out)
    ;;
    ;; 400ns < 2 T-states == 571ns    (no problem at all)
    ;;
    ;; Data sheet, #11.2:
    ;;
    ;; Wait at least 50us after a System Reset before accessing PHY registers.
    ;; Perform an explicit delay here to be absolutely sure.
    ;;
    ;; 64 iterations x (4+12)=16 T-states = 1024 T-states > 288us @3.55MHz
    ;;
    ;; ========================================================================

    xor  a, a
    out  (SPI_OUT), a
    ld   a, #SPI_IDLE
    out  (SPI_OUT), a
00001$:
    dec  a
    jr   nz, 00001$

    ;; ------------------------------------------------------------------------
    ;; poll ESTAT until ESTAT_CLKRDY is set
    ;; ------------------------------------------------------------------------

    ld    e, #ESTAT
    ld    hl, #ESTAT_CLKRDY + 0x0100 * ESTAT_CLKRDY
    call  _enc28j60_poll_register

    ;; ========================================================================
    ;; set up initial register values for ENC28J60
    ;; ========================================================================

    ld    hl, #_eth_register_values

eth_init_registers_loop:

    ld    a, (hl)  ;; register descriptor, 8 bits
    cp    a, #END_OF_TABLE
    jr    z, eth_init_registers_done
    inc   hl

    push  hl
    push  af      ;; stack register descriptor

    ;; ------------------------------------------------------------------------
    ;; select register bank (encoded as bits 5-6 from descriptor)
    ;; ------------------------------------------------------------------------

    rlca          ;; rotate left 3 == rotate right 5
    rlca
    rlca
    and   a, #3
    ld    e, a
    call  _enc28j60_select_bank

    ;; ------------------------------------------------------------------------
    ;; write register value
    ;; ------------------------------------------------------------------------

    pop   af             ;; A is now register descriptor
    and   a, #0x1f       ;; mask out register index (0..1f)
    or    a, #ENC_OPCODE_WCRx
    ld    c, a

    pop   hl
    ld    b, (hl)
    inc   hl

    push  hl         ;; remember position in table
    push  bc         ;; move args
    pop   hl         ;; into HL
    call  _enc28j60_internal_write8plus8
    pop   hl
    jr    eth_init_registers_loop


eth_init_registers_done::

    ld    hl, #ENC28J60_RXBUF_START
    ld    (_next_frame), hl
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
eth_create(void)
__naked
{
  __asm

    push  hl
    or    a, a
    ex    af, af'     ;; ethertype in Z flag in F' (set for IP, clear for ARP)

    ;; ------------------------------------------------------------------------
    ;; select default bank for ENC28J60
    ;; ------------------------------------------------------------------------

    call  _enc28j60_select_bank0

    ;; ------------------------------------------------------------------------
    ;; remember _current_txbuf depending on ethertype
    ;; (TXBUF1 for IP, TXBUF2 for ARP)
    ;; ------------------------------------------------------------------------

    ex    af, af'          ;; bring back ethertype from F'

    ld    hl, #ENC28J60_TXBUF1_START
    jr    z, eth_create_txbuf_set
    ld    hl, #ENC28J60_TXBUF2_START
eth_create_txbuf_set:
    ld    (_current_txbuf), hl

    ex    af, af'          ;; keep ethertype in F'

    ;; ------------------------------------------------------------------------
    ;; set up EWRPT for writing packet data
    ;; ------------------------------------------------------------------------

    ld    a, #ENC_OPCODE_WCR(EWRPTL)
    call  _enc28j60_write_register16

    ;; ========================================================================
    ;; write Ethernet header, including administrative control byte
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; write per-packet control byte  (0x0E; datasheet, section 7.1)
    ;; ------------------------------------------------------------------------

    ld    de, #1
    ld    hl, #eth_create_control_byte
    call  _enc28j60_write_memory_cont

    ;; ------------------------------------------------------------------------
    ;; write destination (remote) MAC address
    ;; ------------------------------------------------------------------------

    pop   hl                             ;; bring back destination MAC address
    call  _enc28j60_write_6b

    ;; ------------------------------------------------------------------------
    ;; write source (local) MAC address
    ;; ------------------------------------------------------------------------

    ld    hl, #_eth_local_address
    call  _enc28j60_write_6b

    ;; ------------------------------------------------------------------------
    ;; write Ethertype
    ;; ------------------------------------------------------------------------

    ld    e, #ETH_SIZEOF_ETHERTYPE           ;; D==0 here
    ld    hl, #ethertype_ip
    ex    af, af'          ;; bring back ethertype from AF'
    jr    z, eth_create_ethertype_set
    ld    hl, #ethertype_arp
eth_create_ethertype_set:
    jp    _enc28j60_write_memory_cont

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
ip_send(void)
__naked
{
  __asm

    ld   hl, (_header_template + 2)   ;; IP length
    ld   a, l  ;; swap byte order in HL
    ld   l, h
    ld   h, a

    ;; FALL THROUGH to eth_send

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
eth_send(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; set DE = start address of frame in transmission buffer,
    ;;     HL = end address of frame in transmission buffer
    ;;
    ;; end address = start
    ;;               + 1 (per-packet control byte)
    ;;               + ETH_HEADER_SIZE
    ;;               + nbr_bytes
    ;;               - 1 (point to last byte, not after it)
    ;;             = start + ETH_HEADER_SIZE + nbr_bytes
    ;; ------------------------------------------------------------------------

    ld    de, (_current_txbuf)
    add   hl, de
    ld    bc, #ETH_HEADER_SIZE
    add   hl, bc

    ;; ------------------------------------------------------------------------
    ;; Check if DE points to a critical frame (BOOTP/TFTP, not ARP). Only
    ;; need to check the low byte.
    ;; ------------------------------------------------------------------------

    ld    a, e
    cp    a, #<ETH_FRAME_PRIORITY
    jr    nz, perform_transmission

    ;; ------------------------------------------------------------------------
    ;; this is a critical frame:
    ;;   update _end_of_critical_frame,
    ;;   and reset retransmission timer
    ;; ------------------------------------------------------------------------

    ld    (_end_of_critical_frame), hl

    ;; skip ld bc, 0 here: BC is 0x0E (ETH_HEADER_SIZE), which is close enough

    ld    (_timer_tick_count), bc
    ld    a, #RETRANSMISSION_TIMEOUT_MIN
    ld    (_retransmission_timeout), a

    ;; FALL THROUGH to perform_transmission

    ;; ########################################################################
    ;; perform_transmission:
    ;;
    ;; Perform a frame transmission. Registers and ETXST and ETXND must be set
    ;; before this function is called.
    ;;
    ;; Does not return until the frame has been transmitted.
    ;;
    ;; DE: address of the first byte in the frame
    ;; HL: address of the last byte in the frame
    ;; ########################################################################

perform_transmission::

      ;; ----------------------------------------------------------------------
      ;; set up registers:  ETXST := start_address, ETXND := end_address
      ;; ----------------------------------------------------------------------

      push  hl   ;; remember HL=end_address
      push  de

      call  _enc28j60_select_bank0     ;; bank of ETXST and ETXND

      pop   hl
      ;; keep end_address on stack

      ld    a, #ENC_OPCODE_WCR(ETXSTL)
      call  _enc28j60_write_register16

      ld    a, #ENC_OPCODE_WCR(ETXNDL)
      pop   hl   ;; end_address pushed above
      call  _enc28j60_write_register16

      ;; ----------------------------------------------------------------------
      ;; Poll for link to come up (if it has not already)
      ;;
      ;; NOTE: this code assumes the MIREGADR/MICMD registers to be configured
      ;;       for continuous scanning of PHSTAT2 -- see eth_init
      ;; ----------------------------------------------------------------------

      ld    e, #BANK(MIRDH)             ;; bank 2
      call  _enc28j60_select_bank

      ;; poll MIRDH until PHSTAT2_HI_LSTAT is set

      ld    e, #MIRDH
      ld    hl, #PHSTAT2_HI_LSTAT * 0x100 + PHSTAT2_HI_LSTAT
      call  _enc28j60_poll_register

      ;; ----------------------------------------------------------------------
      ;; Errata, item 10:
      ;;
      ;; Reset transmit logic before transmitting a frame:
      ;; set bit TXRST in ECON1, then clear it
      ;; ----------------------------------------------------------------------

      call  _enc28j60_select_bank0    ;; bank of ECON1

      ld    hl, #0x0100 * ECON1_TXRST + ENC_OPCODE_BFS(ECON1)
      call  _enc28j60_internal_write8plus8

      ld    hl, #0x0100 * ECON1_TXRST + ENC_OPCODE_BFC(ECON1)
      call  _enc28j60_internal_write8plus8

      ;; ----------------------------------------------------------------------
      ;; clear EIE.TXIE, EIR.TXIF, EIR.TXERIF, ESTAT.TXABRT
      ;; ----------------------------------------------------------------------

      ld    hl, #0x0100 * EIE_TXIE + ENC_OPCODE_BFC(EIE)
      call  _enc28j60_internal_write8plus8

      ld    hl, #0x0100 * (EIR_TXIF + EIR_TXERIF) + ENC_OPCODE_BFC(EIR)
      call  _enc28j60_internal_write8plus8

      ld    hl, #0x0100 * (ESTAT_TXABRT) + ENC_OPCODE_BFC(ESTAT)
      call  _enc28j60_internal_write8plus8

      ;; ----------------------------------------------------------------------
      ;; set ECON1.TXRTS, and poll it until it clears
      ;; ----------------------------------------------------------------------

      ld    hl, #0x0100 * ECON1_TXRTS + ENC_OPCODE_BFS(ECON1)
      call  _enc28j60_internal_write8plus8

      ld    e, #ECON1
      ;; H=ECON1_TXRTS from above, B=0 from _spi_write_byte
      ld    l, b

      jp    _enc28j60_poll_register

  __endasm;
}

/* -------------------------------------------------------------------------
 * Main function: initiate BOOTP, receive frames and act on them
 * ------------------------------------------------------------------------- */

void
main(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; system initialization
    ;; ------------------------------------------------------------------------

#ifdef PAINT_STACK
    call  _paint_stack
#endif

    call  _eth_init
    call  _bootp_init

    ;; ========================================================================
    ;; main loop: receive packets and act on them
    ;; ========================================================================

    ;; At this point, eth_send() has already been called once for a PRIORITY
    ;; frame (BOOTP above), so the ACK timer does not need to be reset.

main_loop::

    ;; ------------------------------------------------------------------------
    ;; Spin here until at least one frame is received. Do this by
    ;; polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
    ;;
    ;; If the re-transmission timer expires, re-send the last frame
    ;; of class 'ETH_FRAME_PRIORITY' (if any), and reset the timer.
    ;; ------------------------------------------------------------------------

    ld    e, #BANK(EPKTCNT)       ;; bank 1
    call  _enc28j60_select_bank

main_spin_loop::

    ld    e, #EPKTCNT
    call  _enc28j60_read_register

    ld    a, c
    or    a, a
    jr    nz, main_packet           ;; NZ means a packet has been received

    ;; ------------------------------------------------------------------------
    ;; check for time-out
    ;; ------------------------------------------------------------------------

    ld    a, (_retransmission_timeout)
    ld    b, a                             ;; remember for later
    ld    a, (_timer_tick_count + 1)       ;; high byte
    cp    a, b
    jr    c, main_spin_loop         ;; carry means no time-out -- keep spinning

    ;; ------------------------------------------------------------------------
    ;; time-out detected: first, reset timer
    ;; ------------------------------------------------------------------------

    xor   a, a      ;; better than ld hl, #0, as we can use A==0 below
    ld    h, a
    ld    l, a
    ld    (_timer_tick_count), hl

    ;; ------------------------------------------------------------------------
    ;; If _end_of_critical_frame has the special value zero, no critical
    ;; frame currently needs retransmission. Only need to check high byte here,
    ;; as the TX buffers are placed at the end of the ENC28J60 address space.
    ;; ------------------------------------------------------------------------

    ld    de, (_end_of_critical_frame)    ;; needed for later
    or    a, d                   ;; assuming A==0
    jr    z, main_spin_loop      ;; nothing to retransmit -- keep spinning

    ;; ------------------------------------------------------------------------
    ;; If _retransmission_timeout >= RETRANSMISSION_TIMEOUT_MAX, give up.
    ;; ------------------------------------------------------------------------

    ld    a, b
    cp    a, #RETRANSMISSION_TIMEOUT_MAX
    ld    a, #FATAL_NO_RESPONSE
    jp    nc, _fail

    ;; ------------------------------------------------------------------------
    ;; double _retransmission_timeout
    ;; ------------------------------------------------------------------------

    ld    a, b
    add   a, a          ;; double timeout
    ld    (_retransmission_timeout), a

    ;; ------------------------------------------------------------------------
    ;; re-send last critical (BOOTP/TFTP) frame
    ;; ------------------------------------------------------------------------

    ld    hl, #ENC28J60_TXBUF1_START
    call  perform_transmission

    jr    main_loop

main_packet::

    ;; ========================================================================
    ;; done spinning: a packet has been received, bring it into Spectrum RAM
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; set ERDPT to _next_frame
    ;; ------------------------------------------------------------------------

    call  _enc28j60_select_bank0

    ld    hl, (_next_frame)
    ld    a, #ENC_OPCODE_WCR(ERDPTL)
    call  _enc28j60_write_register16

    ld    de, #ETH_ADM_HEADER_SIZE
    ld    hl, #_rx_eth_adm
    call  _enc28j60_read_memory_cont

    ;; ------------------------------------------------------------------------
    ;; update _next_frame
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_eth_adm + ETH_ADM_OFFSETOF_NEXT_PTR)
    ld    (_next_frame), hl

    ;; ------------------------------------------------------------------------
    ;; decrease EPKTCNT (by setting bit PKTDEC in ECON2), so as to acknowledge
    ;; the packet has been received from ENC28J60 by this host
    ;; ------------------------------------------------------------------------

    ld    hl, #0x0100 * ECON2_PKTDEC + ENC_OPCODE_BFS(ECON2)
    call  _enc28j60_internal_write8plus8

    ;; ------------------------------------------------------------------------
    ;; ignore broadcasts from this host (duh)
    ;; ------------------------------------------------------------------------

    ld    hl, #_rx_eth_adm + ETH_ADM_OFFSETOF_SRC_ADDR
    ld    de, #_eth_local_address
    ld    b, #ETH_ADDRESS_SIZE
    call  _memory_compare
    jr    z, main_packet_done

    ;; ------------------------------------------------------------------------
    ;; pass packet to IP or ARP, if ethertype matches
    ;; 0x0608 -> IP
    ;; 0x0008 -> ARP
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_eth_adm + ETH_ADM_OFFSETOF_ETHERTYPE)
    ld    a, l
    cp    a, #8
    jr    nz, main_packet_done   ;; neither IP nor ARP -- ignore

    ld    a, h
    or    a, a
    jr    z, main_packet_ip
    cp    a, #6
    jr    nz, main_packet_done
    call  _arp_receive
    jr    main_packet_done
main_packet_ip::
    call  _ip_receive

main_packet_done::

    ;; ------------------------------------------------------------------------
    ;; advance ERXRDPT
    ;; ------------------------------------------------------------------------

    call  _enc28j60_select_bank0     ;; bank of ERXRDPT

    ;; errata B5, item 11:  EXRDPT must always be written with an odd value

    ld    hl, (_next_frame)
    dec   hl
    set   0, l

    ld    a, #ENC_OPCODE_WCR(ERXRDPTL)
    call  _enc28j60_write_register16

    jp    main_loop

  __endasm;
}
