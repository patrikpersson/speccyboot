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

#define timer_expired() \
  (timer_value() > retransmission_timeout)

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
    ;; 64 iterations, each is 13 T-states, 64x13 = 832 T-states > 234 @3.55MHz
    ;;
    ;; ========================================================================

    xor  a, a
    out  (SPI_OUT), a
    ld   a, #SPI_IDLE
    out  (SPI_OUT), a
    ld   b, a
00001$:
    djnz  00001$

    ;; ------------------------------------------------------------------------
    ;; poll ESTAT until ESTAT_CLKRDY is set
    ;; ------------------------------------------------------------------------

    ld    h, #ESTAT
    ld    de, #ESTAT_CLKRDY + 0x0100 * ESTAT_CLKRDY
    call  _enc28j60_poll_register2

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
    push  af      ;; stack bank (0-3)
    inc   sp
    call  _enc28j60_select_bank
    inc   sp

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
    push  bc         ;; push args
    call  _enc28j60_internal_write8plus8
    pop   bc
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
eth_create(const struct mac_address_t *destination,
           uint16_t                    ethertype,
           eth_frame_class_t           frame_class)
__naked
{
  (void) destination, ethertype, frame_class;
  __asm

    push  ix
    ld    ix, #4
    add   ix, sp

    ;; assume
    ;; destination at 0(ix), 1(ix)
    ;; ethertype   at 2(ix), 3(ix)
    ;; frame_class at 4(ix), 5(ix)

    ;; ------------------------------------------------------------------------
    ;; select default bank for ENC28J60
    ;; ------------------------------------------------------------------------

    xor   a, a     ;; ENC28J60_DEFAULT_BANK
    push  af
    inc   sp
    call  _enc28j60_select_bank
    inc   sp

    ;; ------------------------------------------------------------------------
    ;; frame_class maps directly to a buffer;
    ;; store it in _current_txbuf
    ;; ------------------------------------------------------------------------

    ld    l, 4(ix)
    ld    h, 5(ix)
    ld    (_current_txbuf), hl

    ;; ------------------------------------------------------------------------
    ;; set up EWRPT for writing packet data
    ;; ------------------------------------------------------------------------

    push  hl
    ld    hl, #ENC_OPCODE_WCR(EWRPTL) + 0x0100 * ENC_OPCODE_WCR(EWRPTH)
    push  hl
    call  _enc28j60_write_register16_impl
    pop   hl
    pop   hl

    ;; ========================================================================
    ;; write Ethernet header, including administrative control byte
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; write per-packet control byte  (0x0E; datasheet, section 7.1)
    ;; ------------------------------------------------------------------------

    ld    bc, #1
    push  bc
    ld    hl, #eth_create_control_byte
    push  hl
    call  _enc28j60_write_memory_cont
    pop   hl
    pop   bc

    ;; ------------------------------------------------------------------------
    ;; write destination (remote) MAC address
    ;; ------------------------------------------------------------------------

    ;; address of a 0x0E constant byte (instruction LD C, n)

eth_create_control_byte::
    ld    c, #ETH_ADDRESS_SIZE           ;; B==0 here
    push  bc
    ld    l, 0(ix)
    ld    h, 1(ix)
    push  hl
    call  _enc28j60_write_memory_cont
    pop   hl
    pop   bc

    ;; ------------------------------------------------------------------------
    ;; write source (local) MAC address
    ;; ------------------------------------------------------------------------

    ;; BC still correct size
    push  bc
    ld    hl, #_eth_local_address
    push  hl
    call  _enc28j60_write_memory_cont
    pop   hl
    pop   bc

    ;; ------------------------------------------------------------------------
    ;; write Ethertype
    ;; ------------------------------------------------------------------------

    ld    c, #ETH_SIZEOF_ETHERTYPE           ;; B==0 here
    push  bc

    push  ix
    pop   hl
    inc   hl
    inc   hl   ;; points to ethertype on stack
    push  hl
    call  _enc28j60_write_memory_cont
    pop   hl
    pop   bc

    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
eth_send(uint16_t total_nbr_of_bytes_in_payload)
__naked
{
  (void) total_nbr_of_bytes_in_payload;

  __asm

    pop   hl    ;; return address
    pop   bc    ;; total_nbr_of_bytes_in_payload
    push  bc
    push  hl

    ;; ------------------------------------------------------------------------
    ;; set HL = end address of frame in transmission buffer,
    ;;     DE = start address of frame in transmission buffer
    ;;
    ;; end address = start
    ;;               + 1 (per-packet control byte)
    ;;               + ETH_HEADER_SIZE
    ;;               + nbr_bytes
    ;;               - 1 (point to last byte, not after it)
    ;;             = start + ETH_HEADER_SIZE + nbr_bytes
    ;; ------------------------------------------------------------------------

    ld    hl, (_current_txbuf)
    ld    d, h
    ld    e, l
    add   hl, bc
    ld    bc, #ETH_HEADER_SIZE
    add   hl, bc

    ;; ------------------------------------------------------------------------
    ;; check if DE points to a critical frame (BOOTP/TFTP, not ARP)
    ;; ------------------------------------------------------------------------

    ld    a, d
    cp    a, #>ETH_FRAME_PRIORITY
    jr    nz, eth_send_timer_done
    ld    a, e
    cp    a, #<ETH_FRAME_PRIORITY
    jr    nz, eth_send_timer_done

    ;; ------------------------------------------------------------------------
    ;; this is a critical frame:
    ;;   update _end_of_critical_frame,
    ;;   and reset retransmission timer
    ;; ------------------------------------------------------------------------

    ld    (_end_of_critical_frame), hl

    ld    bc, #0
    ld    (_timer_tick_count), bc
    ld    c, #RETRANSMISSION_TIMEOUT_MIN    ;; B==0 here
    ld    (_retransmission_timeout), bc

eth_send_timer_done::

    ;; ------------------------------------------------------------------------
    ;; send Ethernet frame
    ;; ------------------------------------------------------------------------

    push  hl
    push  de
    call  _perform_transmission
    pop   de
    pop   hl

    ret

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

    ld    l, #BANK(EPKTCNT)
    push  hl
    call  _enc28j60_select_bank
    pop   hl

main_spin_loop::

    ld    l, #EPKTCNT
    push  hl
    call  _enc28j60_read_register
    pop   af

    ;; take care to not POP HL above

    ld    a, l
    or    a, a
    jr    nz, main_packet           ;; NZ means a packet has been received

    ;; ------------------------------------------------------------------------
    ;; check for time-out
    ;; ------------------------------------------------------------------------

    ;; The SBC below depends on the C flag, which is zero here (from OR above).
    ;; (for this timeout, +/-1 corresponds to +/-20ms, which would not matter
    ;; anyway)

    ld    hl, (_timer_tick_count)
    ld    bc, (_retransmission_timeout)
    sbc   hl, bc
    jr    c, main_spin_loop       ;; carry means no time-out -- keep spinning

    ;; ------------------------------------------------------------------------
    ;; time-out detected: first, reset timer
    ;; ------------------------------------------------------------------------

    ld    hl, #0
    ld    (_timer_tick_count), hl

    ;; ------------------------------------------------------------------------
    ;; if _end_of_critical_frame has the special value zero, no critical
    ;; frame currently needs retransmission
    ;; ------------------------------------------------------------------------

    ld    de, (_end_of_critical_frame)
    ld    a, d
    or    a, e
    jr    z, main_spin_loop      ;; nothing to retransmit -- keep spinning

    ;; ------------------------------------------------------------------------
    ;; If _retransmission_timeout >= RETRANSMISSION_TIMEOUT_MAX, give up.
    ;; (Check the high byte only, that should be enough.)
    ;; ------------------------------------------------------------------------

    ld    a, b
    cp    a, #>RETRANSMISSION_TIMEOUT_MAX
    ld    a, #FATAL_NO_RESPONSE
    jp    nc, _fail

    ;; ------------------------------------------------------------------------
    ;; double _retransmission_timeout
    ;; ------------------------------------------------------------------------

    add   hl, bc        ;; HL==0, so this means HL := BC
    add   hl, bc        ;; double timeout
    ld    (_retransmission_timeout), hl

    ;; ------------------------------------------------------------------------
    ;; re-send last critical (BOOTP/TFTP) frame
    ;; ------------------------------------------------------------------------

    push  de
    ld    hl, #ENC28J60_TXBUF1_START
    push  hl
    call  _perform_transmission
    pop   hl
    pop   de

    jr    main_loop

main_packet::

    ;; ========================================================================
    ;; done spinning: a packet has been received, bring it into Spectrum RAM
    ;; ========================================================================

    xor   a, a    ;; ENC28J60_DEFAULT_BANK
    push  af
    inc   sp
    call  _enc28j60_select_bank
    inc   sp

    ;; ------------------------------------------------------------------------
    ;; set ERDPT to _next_frame
    ;; ------------------------------------------------------------------------

    ld    hl, (_next_frame)
    push  hl
    ld    hl, #ENC_OPCODE_WCR(ERDPTL) + 0x0100 * ENC_OPCODE_WCR(ERDPTH)
    push  hl
    call  _enc28j60_write_register16_impl
    pop   hl
    pop   hl

    ld    bc, #ETH_ADM_HEADER_SIZE
    push  bc
    ld    hl, #_rx_eth_adm
    push  hl
    call  _enc28j60_read_memory_cont
    pop   hl
    pop   hl

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
    push  hl
    call  _enc28j60_internal_write8plus8
    pop   hl

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

    ld    l, #BANK(ERXRDPTL)
    push  hl
    call  _enc28j60_select_bank
    pop   hl

    ;; errata B5, item 11:  EXRDPT must always be written with an odd value

    ld    hl, (_next_frame)
    dec   hl
    set   0, l

    push  hl
    ld    hl, #ENC_OPCODE_WCR(ERXRDPTL) + 0x0100 * ENC_OPCODE_WCR(ERXRDPTH)
    push  hl
    call  _enc28j60_write_register16_impl
    pop   hl
    pop   hl

    jp    main_loop

  __endasm;
}
