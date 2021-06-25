;; Module eth:
;;
;; Ethernet implementation using the Microchip ENC28J60 Ethernet host.
;; Also handles ARP (RFC 826).
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2009-  Patrik Persson
;;
;; Permission is hereby granted, free of charge, to any person
;; obtaining a copy of this software and associated documentation
;; files (the "Software"), to deal in the Software without
;; restriction, including without limitation the rights to use,
;; copy, modify, merge, publish, distribute, sublicense, and/or sell
;; copies of the Software, and to permit persons to whom the
;; Software is furnished to do so, subject to the following
;; conditions:
;;
;; The above copyright notice and this permission notice shall be
;; included in all copies or substantial portions of the Software.
;;
;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
;; EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
;; OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
;; NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
;; HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
;; WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
;; FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
;; OTHER DEALINGS IN THE SOFTWARE.

    .module eth

    .include "bootp.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================
;; ARP header constants
;; ============================================================================

ARP_OFFSET_SHA =  8         ;; offset of SHA field in ARP header
ARP_OFFSET_SPA = 14         ;; offset of SPA field in ARP header
ARP_OFFSET_TPA = 24         ;; offset of TPA field in ARP header
ARP_IP_ETH_PACKET_SIZE = 28 ;; size of an ARP packet for an IP-Ethernet mapping

;; ============================================================================

ETH_ADM_HEADER_SIZE = 20

    .area _DATA

;; ----------------------------------------------------------------------------
;; ENC28J60 administrative header
;; ----------------------------------------------------------------------------

eth_adm_header:
_next_frame:
    .ds   2             ;; position of next frame to read from the ENC28J60
    .ds   10
eth_sender_address:
    .ds   ETH_ADDRESS_SIZE
eth_adm_header_ethertype:
    .ds   2

;; ============================================================================

_end_of_critical_frame:
    .ds 2                   ;; written to ETXND for re-transmission

;; ============================================================================

    .area _CODE

END_OF_TABLE = ENC28J60_UNUSED_REG   ;; sentinel value for config table below

;; ############################################################################
;; Main function: initiate BOOTP, receive frames and act on them
;; Must be first in the _CODE segment, as init will execute right into it
;; ############################################################################

    ;; ========================================================================
    ;; Set up resident digit font
    ;; ========================================================================

    ld    hl, #_font_data + ('0' - ' ') * 8 +1
    ld    de, #digit_font_data
    ld    bc, #8 * 10 - 2
    ldir

    ;; ========================================================================
    ;; Set initial state for TFTP data handling
    ;; ========================================================================

    ld    hl, #tftp_state_menu_loader
    ld    (tftp_state), hl

    ;; ========================================================================
    ;; Presentation
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; flashing cursor (bottom left): black ink, green paper, bright, flash
    ;; ------------------------------------------------------------------------

    ;; Attribute byte (BLACK | (GREEN << 3) | BRIGHT | FLASH) == 0xE0
    ;; happens to coincide with low byte in the VRAM address

    ld    hl, #ATTRS_BASE + 23 * 32                        ;; (23, 0) -- 0x5AE0
    ld    (hl), l

    ;; ========================================================================
    ;; system initialization
    ;; ========================================================================

    call  eth_init
    
    bootp_init             ;; macro, no need to CALL

    ;; ========================================================================
    ;; main loop: receive packets and act on them
    ;; ========================================================================

    ;; At this point, eth_send() has already been called once for a PRIORITY
    ;; frame (BOOTP above), so the ACK timer does not need to be reset.

main_loop:

    ;; ------------------------------------------------------------------------
    ;; The menu (stage 2) JP:s here to load snapshots.lst.
    ;; Save a few bytes of stack.
    ;; ------------------------------------------------------------------------

    ld    sp, #_stack_top

    ;; ------------------------------------------------------------------------
    ;; Spin here until at least one frame is received. Do this by
    ;; polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
    ;;
    ;; If the re-transmission timer expires, re-send the last frame
    ;; of class 'ETH_FRAME_PRIORITY' (if any), and reset the timer.
    ;; ------------------------------------------------------------------------

    ld    e, #1       ;; bank 1 for EPKTCNT
    rst   enc28j60_select_bank

main_spin_loop:

    ld    e, #EPKTCNT
    call  enc28j60_read_register

    or    a, a
    jr    nz, main_packet           ;; NZ means a packet has been received

    ld    a, (_timer_tick_count + 1)   ;; high byte
    dec   a                            ;; A >= 1 means time-out
    jr    c, main_spin_loop

    ;; ------------------------------------------------------------------------
    ;; Re-transmit the last critical frame
    ;; ------------------------------------------------------------------------

    ld    hl, (_end_of_critical_frame)
    ld    de, #ENC28J60_TXBUF1_START
    call  perform_transmission

jr_main_loop:

    jr    main_loop

main_packet:

    ;; ========================================================================
    ;; done spinning: a packet has been received, bring it into Spectrum RAM
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; set ERDPT to _next_frame
    ;; ------------------------------------------------------------------------

    ld    e, #0
    rst   enc28j60_select_bank

    ld    hl, (_next_frame)
    ld    a, #OPCODE_WCR + (ERDPTL & REG_MASK)
    rst   enc28j60_write_register16

    ;; ------------------------------------------------------------------------
    ;; Read the administrative Ethernet header (20 bytes, including some
    ;; ENC28J60 details).
    ;; ------------------------------------------------------------------------

    ld    de, #ETH_ADM_HEADER_SIZE
    ld    hl, #eth_adm_header
    call  enc28j60_read_memory

    ;; ------------------------------------------------------------------------
    ;; decrease EPKTCNT (by setting bit PKTDEC in ECON2), so as to acknowledge
    ;; the packet has been received from ENC28J60 by this host
    ;; ------------------------------------------------------------------------

    ld    hl, #0x0100 * ECON2_PKTDEC + OPCODE_BFS + (ECON2 & REG_MASK)
    rst   enc28j60_write8plus8

    ;; ------------------------------------------------------------------------
    ;; pass packet to IP or ARP, if ethertype matches
    ;; 0x0608 -> IP
    ;; 0x0008 -> ARP
    ;; ------------------------------------------------------------------------

    ;; HL points to Ethertype after LDIR above

    ld    hl, (eth_adm_header_ethertype)
    ld    a, l
    sub   a, #8
    jr    nz, main_packet_done   ;; neither IP nor ARP -- ignore
    or    a, h
    jr    nz, main_packet_not_ip

    call  ip_receive
    xor   a, a                   ;; avoid matching ARP below

main_packet_not_ip:
    cp    a, #6
    call  z, arp_receive

main_packet_done:

    ;; ------------------------------------------------------------------------
    ;; advance ERXRDPT
    ;; ------------------------------------------------------------------------

    ld    e, #0     ;; bank of ERXRDPT
    rst   enc28j60_select_bank

    ;; errata B5, item 11:  EXRDPT must always be written with an odd value

    ld    hl, (_next_frame)
    dec   hl
    set   0, l

    ld    a, #OPCODE_WCR + (ERXRDPTL & REG_MASK)
    rst   enc28j60_write_register16

    jr    jr_main_loop


;; ############################################################################
;; eth_init
;; ############################################################################

eth_init:

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
    call  poll_register

    ;; ------------------------------------------------------------------------
    ;; ensure _next_frame gets a sensible value before any frame is loaded
    ;; ------------------------------------------------------------------------

    ld    hl, #ENC28J60_RXBUF_START
    ld    (_next_frame), hl

    ;; ========================================================================
    ;; set up initial register values for ENC28J60
    ;; ========================================================================

    ld    hl, #eth_register_defaults

eth_init_registers_loop:

    ld    a, (hl)  ;; register descriptor, 8 bits
    cp    a, #END_OF_TABLE
    ret   z

    inc   hl
    ld    d, a

    and   a, #0x1f       ;; mask out register index (0..1f)
    or    a, #OPCODE_WCR
    ld    c, a

    ld    b, (hl)
    inc   hl

    push  bc         ;; arguments for enc28j60_write8plus8 below

    ;; ------------------------------------------------------------------------
    ;; select register bank (encoded as bits 5-6 from descriptor)
    ;; ------------------------------------------------------------------------

    ld    a, d    ;; recall register descriptor

    exx           ;; keep HL

    rlca          ;; rotate left 3 == rotate right 5
    rlca
    rlca
    and   a, #3
    ld    e, a
    rst   enc28j60_select_bank

    ;; ------------------------------------------------------------------------
    ;; write register value
    ;; ------------------------------------------------------------------------

    pop   hl         ;; into HL
    rst   enc28j60_write8plus8

    exx

    jr    eth_init_registers_loop

    ;; ------------------------------------------------------------------------
    ;; ETH register defaults for initialization
    ;;
    ;; Since the ENC28J60 does not support auto-negotiation, we will need to
    ;; stick to half duplex. Not a problem, since Ethernet performance is not
    ;; really a bottleneck on the Spectrum.
    ;; ------------------------------------------------------------------------

eth_register_defaults:
    .db   ERXSTL,   <ENC28J60_RXBUF_START
    .db   ERXSTH,   >ENC28J60_RXBUF_START

    .db   ERXNDL,   <ENC28J60_RXBUF_END
    .db   ERXNDH,   >ENC28J60_RXBUF_END

    ;; B5 errata, item 11: only odd values are allowed when writing ERXRDPT
    .db   ERXRDPTL, <ENC28J60_RXBUF_END
    .db   ERXRDPTH, >ENC28J60_RXBUF_END

    .db   ERXFCON,  ERXFCON_CRCEN

    ;; MAC initialization: half duplex
    .db   MACON1,   MACON1_MARXEN

    ;; MACON3: set bits PADCFG0..2 to pad frames to at least 64B, append CRC
    .db   MACON3,   0xE0 + MACON3_TXCRCEN
    .db   MACON4,   MACON4_DEFER

    .db   MAMXFLL,  <ETH_MAX_RX_FRAME_SIZE
    .db   MAMXFLH,  >ETH_MAX_RX_FRAME_SIZE

    .db   MABBIPG,  0x12    ;; as per datasheet section 6.5
    .db   MAIPGL,   0x12    ;; as per datasheet section 6.5
    .db   MAIPGH,   0x0C    ;; as per datasheet section 6.5

    .db   MAADR1,   MAC_ADDR_0
    .db   MAADR2,   MAC_ADDR_1
    .db   MAADR3,   MAC_ADDR_2
    .db   MAADR4,   MAC_ADDR_3
    .db   MAADR5,   MAC_ADDR_4
    .db   MAADR6,   MAC_ADDR_5

    ;; PHY initialization

    .db   MIREGADR, PHCON1
    .db   MIWRL,    0x00     ;; PHCON1 := 0x0000 -- half duplex
    .db   MIWRH,    0x00

    ;; Set up PHY to automatically scan the PHSTAT2 every 10.24 us
    ;; (the current value can then be read directly from MIRD)

    .db   MIREGADR, PHSTAT2
    .db   MICMD,    MICMD_MIISCAN

    ;; Enable reception and transmission
    .db   EIE,      0x00     ;; disable all interrupts
    .db   EIR,      0x00     ;; no pending interrupts
    .db   ECON2,    ECON2_AUTOINC
    .db   ECON1,    ECON1_RXEN

    .db   END_OF_TABLE

;; ############################################################################
;; eth_create
;; ############################################################################

eth_create:

    ;; ------------------------------------------------------------------------
    ;; remember _current_txbuf (TXBUF1 for IP, TXBUF2 for ARP)
    ;; ------------------------------------------------------------------------

    ld    (_current_txbuf), hl

    push  de
    push  bc
    push  hl

    ;; ------------------------------------------------------------------------
    ;; select default bank for ENC28J60
    ;; ------------------------------------------------------------------------

    ld    e, #0
    rst   enc28j60_select_bank

    pop   hl

    ;; ------------------------------------------------------------------------
    ;; set up EWRPT for writing packet data
    ;; ------------------------------------------------------------------------

    ld    a, #OPCODE_WCR + (EWRPTL & REG_MASK)
    rst   enc28j60_write_register16

    ;; ========================================================================
    ;; write Ethernet header, including administrative control byte
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; write per-packet control byte  (0x0E; datasheet, section 7.1)
    ;; ------------------------------------------------------------------------

    rst   enc28j60_write_memory_inline

    .db   1, 0x0e

    ;; ------------------------------------------------------------------------
    ;; write destination (remote) MAC address
    ;; ------------------------------------------------------------------------

    pop   hl                             ;; bring back destination MAC address
    ld    e, #ETH_ADDRESS_SIZE
    rst   enc28j60_write_memory_small

    ;; ------------------------------------------------------------------------
    ;; write source (local) MAC address
    ;; ------------------------------------------------------------------------

    call  enc28j60_write_local_hwaddr

    ;; ------------------------------------------------------------------------
    ;; write Ethertype
    ;; ------------------------------------------------------------------------

    ld    e, #ETH_SIZEOF_ETHERTYPE
    pop   hl     ;; pop Ethertype pointer
    rst   enc28j60_write_memory_small
    ret

;; ############################################################################
;; arp_receive
;; ############################################################################

arp_receive:

    ;; ------------------------------------------------------------------------
    ;; retrieve ARP payload
    ;; ------------------------------------------------------------------------

    ld   de, #ARP_IP_ETH_PACKET_SIZE
    call enc28j60_read_memory_to_rxframe

    ;; ------------------------------------------------------------------------
    ;; check header against template
    ;; (ARP_OPER_REQUEST, ETHERTYPE_IP, ETH_HWTYPE)
    ;; ------------------------------------------------------------------------

    ;; first check everything except OPER

    ;; HL is set to _rx_frame and preserved by enc28j60_read_memory_to_rxframe

    ld   de, #arp_header_template_start
    ld   b, #(arp_header_template_end - arp_header_template_start - 1)
    call memory_compare
    ret  nz   ;; if the receive packet does not match the expected header, return

    ;; HL now points to the low-order OPER byte, expected to be 1 (REQUEST)
    ld   a, (hl)
    dec  a
    ret  nz

    ;; ------------------------------------------------------------------------
    ;; check that a local IP address has been set,
    ;; and that the packet was sent to this address
    ;; ------------------------------------------------------------------------

    ld   l, #<_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld   a, (hl)
    or   a, a
    ret  z

    ld   de , #_rx_frame + ARP_OFFSET_TPA
    call memory_compare_4_bytes
    ret  nz   ;; if the packet is not for the local IP address, return

    ld   bc, #eth_sender_address
    ld   de, #ethertype_arp
    ld   hl, #ENC28J60_TXBUF2_START
    call eth_create

    ;; ARP header

    rst enc28j60_write_memory_inline

    ;; -----------------------------------------------------------------------
    ;; inline data for enc28j60_write_memory_inline: ARP reply header
    ;; -----------------------------------------------------------------------

    .db  arp_header_template_end - arp_header_template_start         ;; length

arp_header_template_start:
    .db  0, ETH_HWTYPE         ;; HTYPE: 16 bits, network order
ethertype_ip:
    .db  8, 0                  ;; PTYPE: ETHERTYPE_IP, 16 bits, network order
    .db  ETH_ADDRESS_SIZE      ;; HLEN (Ethernet)
    .db  IPV4_ADDRESS_SIZE     ;; PLEN (IPv4)
    .db  0, 2                  ;; OPER: reply, 16 bits, network order
arp_header_template_end:

    ;; -----------------------------------------------------------------------

    ;; SHA: local MAC address

    call enc28j60_write_local_hwaddr

    ;; SPA: local IPv4 address

    ld   e, #IPV4_ADDRESS_SIZE
    ld   hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    rst  enc28j60_write_memory_small

    ;; THA

    ld   e, #ETH_ADDRESS_SIZE
    ld   l, #<_rx_frame + ARP_OFFSET_SHA  ;; sender MAC address, taken from SHA field in request
    rst  enc28j60_write_memory_small

    ;; TPA

    ld   l, #<_rx_frame + ARP_OFFSET_SPA  ;; sender IP address, taken from SPA field in request
    ld   e, #IPV4_ADDRESS_SIZE
    rst  enc28j60_write_memory_small

    ld   hl, #ARP_IP_ETH_PACKET_SIZE
    jr   eth_send


;; ############################################################################
;; ip_send
;; ############################################################################

tftp_read_request:

    prepare_tftp_read_request

    ;; FALL THROUGH to ip_send


;; ############################################################################
;; ip_send
;; ############################################################################

ip_send:

    ld   hl, (_header_template + 2)   ;; IP length
    ld   a, l  ;; swap byte order in HL
    ld   l, h
    ld   h, a

    ;; FALL THROUGH to eth_send

;; ############################################################################
;; eth_send
;; ############################################################################

eth_send:

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
    ;; this is a critical frame: update _end_of_critical_frame
    ;; ------------------------------------------------------------------------

    ld    (_end_of_critical_frame), hl

    ;; FALL THROUGH to perform_transmission


;; ############################################################################
;; perform_transmission:
;;
;; Perform a frame transmission. Registers and ETXST and ETXND must be set
;; before this function is called.
;;
;; Does not return until the frame has been transmitted.
;;
;; DE: address of the first byte in the frame
;; HL: address of the last byte in the frame
;; ############################################################################

perform_transmission:

      ;; ----------------------------------------------------------------------
      ;; set up registers:  ETXST := start_address, ETXND := end_address
      ;; ----------------------------------------------------------------------

      push  hl   ;; remember HL=end_address
      push  de

      ld    e, #0     ;; bank of ETXST and ETXND
      rst   enc28j60_select_bank

      pop   hl
      ;; keep end_address on stack

      ld    a, #OPCODE_WCR + (ETXSTL & REG_MASK)
      rst   enc28j60_write_register16

      ld    a, #OPCODE_WCR + (ETXNDL & REG_MASK)
      pop   hl   ;; end_address pushed above
      rst  enc28j60_write_register16

    ;; ------------------------------------------------------------------------
    ;; Reset retransmission timer. For simplicity, this is done here
    ;; regardless of whether this is a critical frame or not, so it will
    ;; be reset even for an ARP response. 
    ;;
    ;; In theory, an IP stack that keeps sending ARP requests more frequently
    ;; than the retransmission timer (a couple of seconds) could inhibit
    ;; retransmission. In practice, this is not expected to happen.
    ;; ------------------------------------------------------------------------

    ld    hl, #0
    ld    (_timer_tick_count), hl

      ;; ----------------------------------------------------------------------
      ;; Poll for link to come up (if it has not already)
      ;;
      ;; NOTE: this code assumes the MIREGADR/MICMD registers to be configured
      ;;       for continuous scanning of PHSTAT2 -- see eth_init
      ;; ----------------------------------------------------------------------

      ld    e, #2             ;; bank 2 for MIRDH
      rst   enc28j60_select_bank

      ;; poll MIRDH until PHSTAT2_HI_LSTAT is set

      ld    e, #MIRDH
      ld    hl, #PHSTAT2_HI_LSTAT * 0x100 + PHSTAT2_HI_LSTAT
      call  poll_register

      ;; ----------------------------------------------------------------------
      ;; Errata, item 10:
      ;;
      ;; Reset transmit logic before transmitting a frame:
      ;; set bit TXRST in ECON1, then clear it
      ;; ----------------------------------------------------------------------

      ld    e, #0    ;; bank of ECON1
      rst   enc28j60_select_bank

      ld    hl, #0x0100 * ECON1_TXRST + OPCODE_BFS + (ECON1 & REG_MASK)
      rst   enc28j60_write8plus8

      ld    hl, #0x0100 * ECON1_TXRST + OPCODE_BFC + (ECON1 & REG_MASK)
      rst   enc28j60_write8plus8

      ;; ----------------------------------------------------------------------
      ;; clear EIE.TXIE, EIR.TXIF, EIR.TXERIF, ESTAT.TXABRT
      ;; ----------------------------------------------------------------------

      ld    hl, #0x0100 * EIE_TXIE + OPCODE_BFC + (EIE & REG_MASK)
      rst   enc28j60_write8plus8

      ld    hl, #0x0100 * (EIR_TXIF + EIR_TXERIF) + OPCODE_BFC + (EIR & REG_MASK)
      rst   enc28j60_write8plus8

      ld    hl, #0x0100 * (ESTAT_TXABRT) + OPCODE_BFC + (ESTAT & REG_MASK)
      rst   enc28j60_write8plus8

      ;; ----------------------------------------------------------------------
      ;; set ECON1.TXRTS, and poll it until it clears
      ;; ----------------------------------------------------------------------

      ld    hl, #0x0100 * ECON1_TXRTS + OPCODE_BFS + (ECON1 & REG_MASK)
      rst   enc28j60_write8plus8

      ld    e, #ECON1
      ;; H=ECON1_TXRTS from above, B=0 from _spi_write_byte
      ld    l, b

      ;; FALL THROUGH to poll_register


;; ----------------------------------------------------------------------------
;; Subroutine: poll indicated ETH/MAC/MII register R until
;;
;;   (reg & mask) == expected_value
;;
;; Fails if the condition is not fulfilled within a few seconds.
;;
;; Call with registers:
;;
;; E=reg
;; H=mask
;; L=expected_value
;;
;; Destroys AF, BC
;; ----------------------------------------------------------------------------

poll_register:

    ld     bc, #20000       ;; should give controller plenty of time to respond
00001$:
    push   bc
    call   enc28j60_read_register
    pop    bc

    and    a, h
    cp     a, l
    ret    z

    dec    bc
    ld     a, b
    or     a, c
    jr     nz, 00001$

    ld     a, #FATAL_INTERNAL_ERROR
    jp     fail

