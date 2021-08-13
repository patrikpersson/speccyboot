;; Module stack:
;;
;; Integrated stack for Ethernet, IP, ARP, UDP, BOOTP, TFTP.
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

    .module stack

    .include "bootp.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "menu.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"
    .include "z80_loader.inc"

;; ============================================================================
;; ARP header constants
;; ============================================================================

ARP_OFFSET_OPER =  6        ;; offset of OPER field in ARP header
ARP_OFFSET_SHA  =  8        ;; offset of SHA field in ARP header
ARP_OFFSET_SPA  = 14        ;; offset of SPA field in ARP header
ARP_OFFSET_TPA  = 24        ;; offset of TPA field in ARP header
ARP_IP_ETH_PACKET_SIZE = 28 ;; size of an ARP packet for an IP-Ethernet mapping

;; ============================================================================

ETH_ADM_HEADER_SIZE = 20

    .area _DATA

;; ============================================================================
;; Ethernet
;; ============================================================================

;; ----------------------------------------------------------------------------
;; ENC28J60 administrative header
;; ----------------------------------------------------------------------------

eth_adm_header:
next_frame_to_read:
    .ds   2             ;; position of next frame to read from the ENC28J60
    .ds   10
eth_sender_address:
    .ds   ETH_ADDRESS_SIZE
eth_adm_header_ethertype:
    .ds   2

;; ============================================================================
;; IP
;; ============================================================================

;; ----------------------------------------------------------------------------
;; IP checksum
;; ----------------------------------------------------------------------------

_ip_checksum:
    .ds   2

;; ----------------------------------------------------------------------------
;; retransmission counter (number of HALTs executed)
;; ----------------------------------------------------------------------------

retransmission_count:
    .ds   1

;; ============================================================================
;; TFTP
;; ============================================================================

;; ----------------------------------------------------------------------------
;; Position to write loaded TFTP data to (initialized in init.asm)
;; ----------------------------------------------------------------------------

_tftp_write_pos:
   .ds   2

;; ----------------------------------------------------------------------------
;; Number of bytes remaining in current .z80 chunk (used by .z80 loader)
;; ----------------------------------------------------------------------------

_chunk_bytes_remaining:
   .ds   2

;; ----------------------------------------------------------------------------
;; function called for every received TFTP packet
;; ----------------------------------------------------------------------------

tftp_state:
    .ds    2

;; ============================================================================

    .area _CODE

;; ############################################################################
;; Main function: initiate BOOTP, receive frames and act on them
;; Must be first in the _CODE segment, as init will execute right into it
;; ############################################################################

    ;; ------------------------------------------------------------------------
    ;; present heading and flashing cursor, to indicate BOOTP is working
    ;; ------------------------------------------------------------------------

    ld   hl, #heading
    ld   de, #HEADING_POS
    call print_line

    ;; ------------------------------------------------------------------------
    ;; BLACK INK + GREEN PAPER + FLASH + BRIGHT == 0xe0 == <LOCAL_IP_ATTR
    ;; ------------------------------------------------------------------------

    ld   hl, #LOCAL_IP_ATTR
    ld   (hl), l

    ;; ========================================================================
    ;; system initialization
    ;; ========================================================================



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
    ;; ========================================================================

    xor  a, a                      ;; RST low
    out  (SPI_OUT), a
    ld   a, #SPI_IDLE              ;; RST high again
    out  (SPI_OUT), a

    ;; ------------------------------------------------------------------------
    ;; Data sheet, #2.2:
    ;;
    ;; Wait for the OST (Oscillator Startup Timer) to become ready. The data
    ;; sheet indicates this takes about 300 us.
    ;;
    ;; Wait for three frame ticks (40-60 ms). This is obviously overkill, but
    ;; saves the hassle of polling ESTAT.CLKRDY.
    ;;
    ;; This delay also covers the requirement in #11.2:
    ;;
    ;; Wait at least 50us after a System Reset before accessing PHY registers.
    ;; ------------------------------------------------------------------------

    ld   b, #50
delay1:
    halt
    djnz delay1

    ;; ========================================================================
    ;; set up initial register values for ENC28J60
    ;; ========================================================================

    ld    hl, #eth_register_defaults

eth_init_registers_loop:

    ld    c, (hl)                               ;; register descriptor, 8 bits

    inc   hl

    ;; ------------------------------------------------------------------------
    ;; build opcode in C, as 0x40 | (register id), and bank (0..3) in A
    ;;
    ;; shift in bits 1+0 to C, shift out bank bits 1 and 0 into A
    ;; (this shifting is the reason why the bank bits are reversed in the
    ;; table below)
    ;; ------------------------------------------------------------------------

    xor   a, a

    scf
    rr    c      ;; shift 1 into A bit 7, shift out bank bit 1
    rla          ;; shift bank bit 1 into C bit 0, shift out 0
    rr    c      ;; shift 0 into A bit 7, shift out bank bit 0
    rla          ;; shift bank bit 0 into C bit 0

    ;; ------------------------------------------------------------------------
    ;; now C == opcode, set B := register value
    ;; ------------------------------------------------------------------------

    ld    b, (hl)
    inc   hl

    push  bc                                             ;; push opcode + value

    ;; ------------------------------------------------------------------------
    ;; select register bank
    ;; ------------------------------------------------------------------------

    exx           ;; keep HL

    ld    e, a
    rst   enc28j60_select_bank

    ;; ------------------------------------------------------------------------
    ;; write register value
    ;; ------------------------------------------------------------------------

    pop   hl                                           ;; recall opcode + value
    rst   enc28j60_write8plus8

    exx

    bit   7, (hl)                                             ;; end of table?
    jr    z, eth_init_registers_loop

    ;; -----------------------------------------------------------------------
    ;; The ENC28J60 has facilities for waiting for the link to become active,
    ;; but this doesn't seem to be quite enough: the outgoing BOOTP REQUEST
    ;; is frequently missed by the server if sent immediately. An explicit
    ;; delay is introduced here to give the server a better chance.
    ;;
    ;; The data sheet (section 14.0) says that "After leaving Sleep mode,
    ;; there is a delay of many milliseconds before a new link is established
    ;; (assuming an appropriate link partner is present)." Though this refers
    ;; to waking up from sleep mode, it is taken here as an indication that
    ;; the link normally comes up in "many milliseconds". This is vague.
    ;;
    ;; A delay of three frame interrupts (40-60 milliseconds) is assumed to be
    ;; enough here. This is admittedly crude, but if the link takes longer
    ;; time than this to come up, all that happens it that we will have to
    ;; wait for the (then lost) BOOTP REQUEST to be retransmitted. This is
    ;; not critical, but seems to work (at least) as good as polling for an
    ;; active link.
    ;; -----------------------------------------------------------------------
    
    halt
    halt
    halt

    BOOTP_INIT

    ;; ========================================================================
    ;; main loop: receive packets and act on them
    ;; ========================================================================

    ;; At this point, udp_send() has already been called once (BOOTP above),
    ;; so the ACK timer does not need to be reset.

main_loop:

    ;; ------------------------------------------------------------------------
    ;; Spin here until at least one frame is received. Do this by
    ;; polling EPKTCNT. (Errata rev B5, item #4: do not trust EIR.PKTIF)
    ;;
    ;; If the re-transmission timer expires, re-send the last frame
    ;; of class 'ETH_FRAME_PRIORITY' (if any), and reset the timer.
    ;; ------------------------------------------------------------------------

    ld    e, #1                                           ;; bank 1 for EPKTCNT
    rst   enc28j60_select_bank

    ld    c, #EPKTCNT
    call  enc28j60_read_register

    ;; ------------------------------------------------------------------------
    ;; restore register bank 0, keep EPKTCNT in A' for use below
    ;; ------------------------------------------------------------------------

    ex    af, af'                                   ;; keep EPKTCNT value in A'

    ld    e, b                            ;; B == 0 from enc28j60_read_register
    rst   enc28j60_select_bank

    ex    af, af'                         ;; restore EPKTCNT value from A' to A

    ;; ------------------------------------------------------------------------
    ;; any packet received?
    ;; ------------------------------------------------------------------------

    or    a, a
    jr    nz, packet_received

    ;; ------------------------------------------------------------------------
    ;; Wait one frame tick,
    ;; re-transmit the last critical frame if timer expired
    ;; ------------------------------------------------------------------------

    halt

    ld    hl, #retransmission_count
    inc   (hl)

    call  z, udp_send

    jr    main_loop

    ;; ------------------------------------------------------------------------

packet_received:

    ;; ========================================================================
    ;; done spinning: a packet has been received, bring it into Spectrum RAM
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; set ERDPT (bank 0) to next_frame_to_read
    ;; ------------------------------------------------------------------------

    ld    hl, (next_frame_to_read)
    push  hl                                        ;; stack next_frame_to_read

    ld    a, #OPCODE_WCR | ERDPTL
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

    ld    hl, #(0x0100 * ECON2_PKTDEC)  +  (OPCODE_BFS | ECON2)
    rst   enc28j60_write8plus8

    ;; ------------------------------------------------------------------------
    ;; check first byte of Ethertype, common to IP and ARP
    ;; ------------------------------------------------------------------------

    ld    hl, (eth_adm_header_ethertype)
    ld    a, l
    sub   a, #8

    call  z, handle_ip_or_arp_packet

    ;; ------------------------------------------------------------------------
    ;; advance ERXRDPT                  (assuming bank 0 is currently selected)
    ;; ------------------------------------------------------------------------

    ;; errata B5, item 11:  EXRDPT must always be written with an odd value

    pop   hl                                       ;; recall next_frame_to_read

    dec   hl
    set   0, l

    ld    a, #OPCODE_WCR | ERXRDPTL
    rst   enc28j60_write_register16

    jr    main_loop


    ;; ------------------------------------------------------------------------
    ;; ETH register defaults for initialization
    ;;
    ;; Each entry is a pair of bytes: register + value
    ;;
    ;; register byte format:
    ;;
    ;;   bit 0:       bank bit 1      (NOTE: reversed bit order!)
    ;;   bit 1:       bank bit 0
    ;;   bits 2..6:   register id, 5 bits
    ;;   bit 7:       stop bit (0 for data bytes, 1 means end of data)
    ;;
    ;; Since the ENC28J60 does not support auto-negotiation, we will need to
    ;; stick to half duplex. Not a problem, since Ethernet performance is not
    ;; really a bottleneck here. (The bottleneck is instead SPI communication
    ;; from the ENC28J60 to the Spectrum.)
    ;; ------------------------------------------------------------------------

    ;; ------------------------------------------------------------------------
    ;; values for bank selection, with bits reversed (see loop above for why)
    ;; ------------------------------------------------------------------------

BANK_0 = 0x00
BANK_1 = 0x02
BANK_2 = 0x01
BANK_3 = 0x03

eth_register_defaults:
    .db   (ERXSTL << 2)   | BANK_0,    <ENC28J60_RXBUF_START
    .db   (ERXSTH << 2)   | BANK_0,    >ENC28J60_RXBUF_START

    .db   (ERXNDL << 2)   | BANK_0,    <ENC28J60_RXBUF_END
    .db   (ERXNDH << 2)   | BANK_0,    >ENC28J60_RXBUF_END

    ;; B5 errata, item 11: only odd values are allowed when writing ERXRDPT
    .db   (ERXRDPTL << 2) | BANK_0,    <ENC28J60_RXBUF_END
    .db   (ERXRDPTH << 2) | BANK_0,    >ENC28J60_RXBUF_END

    ;; MAC initialization: half duplex
    .db   (MACON1 << 2)   | BANK_2,    MACON1_MARXEN

    ;; MACON3: set bits PADCFG0..2 to pad frames to at least 64B, append CRC
    .db   (MACON3 << 2)   | BANK_2,    0xE0 + MACON3_TXCRCEN
    .db   (MACON4 << 2)   | BANK_2,    MACON4_DEFER

    .db   (MAMXFLL << 2)  | BANK_2,    <ETH_MAX_RX_FRAME_SIZE
    .db   (MAMXFLH << 2)  | BANK_2,    >ETH_MAX_RX_FRAME_SIZE

    .db   (MABBIPG << 2)  | BANK_2,    0x12    ;; as per datasheet section 6.5
    .db   (MAIPGL << 2)   | BANK_2,    0x12    ;; as per datasheet section 6.5
    .db   (MAIPGH << 2)   | BANK_2,    0x0C    ;; as per datasheet section 6.5

    .db   (MAADR1 << 2)   | BANK_3,    MAC_ADDR_0
    .db   (MAADR2 << 2)   | BANK_3,    MAC_ADDR_1
    .db   (MAADR3 << 2)   | BANK_3,    MAC_ADDR_2
    .db   (MAADR4 << 2)   | BANK_3,    MAC_ADDR_3
    .db   (MAADR5 << 2)   | BANK_3,    MAC_ADDR_4
    .db   (MAADR6 << 2)   | BANK_3,    MAC_ADDR_5

    ;; PHY initialization

    .db   (MIREGADR << 2) | BANK_2,    PHCON1
    .db   (MIWRL << 2)    | BANK_2,    0x00                ;; PHCON1 := 0x0000
    .db   (MIWRH << 2)    | BANK_2,    0x00                ;; -- half duplex

    ;; Enable reception
    .db   (ECON1 << 2)    | BANK_0,    ECON1_RXEN

    ;; -----------------------------------------------------------------------
    ;; NOTE: no explicit sentinel here. The table is terminated by a byte
    ;; with bit 7 set, which happens to be the first byte of eth_create below
    ;; (0xD5 == PUSH DE)
    ;; -----------------------------------------------------------------------

;; ###########################################################################
;; eth_create
;; ###########################################################################

eth_create:

    ;; -----------------------------------------------------------------------
    ;; NOTE: the first instruction here (0xD5) terminates the table above.
    ;; -----------------------------------------------------------------------

    push  de                                                ;; stack Ethertype
    push  bc                                  ;; stack destination MAC address

    ;; -----------------------------------------------------------------------
    ;; set up EWRPT for writing packet data                  (assuming bank 0)
    ;; -----------------------------------------------------------------------

    ld    a, #OPCODE_WCR | EWRPTL
    rst   enc28j60_write_register16

    ;; =======================================================================
    ;; write Ethernet header, including administrative control byte
    ;; =======================================================================

    ;; -----------------------------------------------------------------------
    ;; write per-packet control byte  (0x0E; datasheet, section 7.1)
    ;; -----------------------------------------------------------------------

    rst   enc28j60_write_memory_inline

    .db   1, 0x0e

    ;; -----------------------------------------------------------------------
    ;; write destination (remote) MAC address
    ;; -----------------------------------------------------------------------

    ld    e, #ETH_ADDRESS_SIZE
    pop   hl                                 ;; recall destination MAC address
    rst   enc28j60_write_memory_small

    ;; -----------------------------------------------------------------------
    ;; write source (local) MAC address
    ;; -----------------------------------------------------------------------

    call  enc28j60_write_local_hwaddr

    ;; -----------------------------------------------------------------------
    ;; write Ethertype
    ;; -----------------------------------------------------------------------

    ld    e, #ETH_SIZEOF_ETHERTYPE
    pop   hl                                               ;; recall Ethertype
    rst   enc28j60_write_memory_small
    ret


;; ###########################################################################
;; udp_create
;; ###########################################################################

udp_create:

    push  hl

    ;; -----------------------------------------------------------------------
    ;; Set up a header template, to be filled in with proper data below.
    ;; Only the first part of the header is copied here, up to (but not
    ;; including) source/destination IP addresses.
    ;; -----------------------------------------------------------------------

    push  de

    ld    hl, #ip_header_defaults
    ld    de, #outgoing_header
    ld    c, #12                                   ;; B == 0 from precondition

    ldir

    pop   hl         ;; recall UDP length

    ;; -----------------------------------------------------------------------
    ;; set UDP length (network order)
    ;; -----------------------------------------------------------------------

    ld    (outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH), hl

    ;; -----------------------------------------------------------------------
    ;; Add IPV4_HEADER_SIZE to HL. This addition generates no carry, as HL
    ;; holds one of the following lengths (byte-swapped to network order):
    ;;
    ;; BOOTP boot request:
    ;;   UDP_HEADER_SIZE + BOOTP_PACKET_SIZE
    ;;   = 308 = 0x134    (so lower byte is 0x34)
    ;;
    ;; TFTP read request:
    ;;   UDP_HEADER_SIZE + TFTP_SIZE_OF_RRQ_PREFIX + TFTP_SIZE_OF_RRQ_OPTION
    ;;   = 16 = 0x10
    ;;
    ;; TFTP ACK:
    ;;   UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET
    ;;   = 12 = 0x0c
    ;;
    ;; In all these cases, the lower byte (that is, H in network order)
    ;; is < 0xec, so adding IPV4_HEADER_SIZE = 0x14 to H generates no carry.
    ;; -----------------------------------------------------------------------

    ld    b, #IPV4_HEADER_SIZE      ;; least significant byte in network order
    add   hl, bc                    ;; C == 0 after LDIR above

    ;; -----------------------------------------------------------------------
    ;; prepare IP header in outgoing_header
    ;; -----------------------------------------------------------------------

    ld    (outgoing_header + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH), hl

    ;; ----------------------------------------------------------------------
    ;; compute checksum of IP header
    ;; ----------------------------------------------------------------------

    sbc   hl, hl              ;; carry == 0 from ADD HL, BC above, so HL := 0

    ld    b, #(IPV4_HEADER_SIZE / 2)                  ;; number of words (10)
    ld    de, #outgoing_header
    call  enc28j60_add_to_checksum_hl

    ;; ----------------------------------------------------------------------
    ;; store one-complemented checksum
    ;; ----------------------------------------------------------------------

    ld     e, #<outgoing_header + IPV4_HEADER_OFFSETOF_CHECKSUM + 1
    ld     a, h
    cpl
    ld     (de), a
    dec    de
    ld     a, l
    cpl
    ld     (de), a

    ;; ----------------------------------------------------------------------
    ;; create IP packet
    ;; ----------------------------------------------------------------------

    pop    bc                                      ;; destination MAC address
    ld     de, #ethertype_ip
    ld     hl, #ENC28J60_TXBUF1_START
    call   eth_create

    ;; ----------------------------------------------------------------------
    ;; Write IPv4 + UDP headers. The UDP checksum remains zero here,
    ;; so the UDP checksum is not used in outgoing packets.
    ;; ----------------------------------------------------------------------

    ld     e, #IPV4_HEADER_SIZE + UDP_HEADER_SIZE

    ;; FALL THROUGH and continue execution in the IP header defaults

;; ===========================================================================
;; IP header defaults
;; https://en.wikipedia.org/wiki/IPv4#Header
;;
;; Warning: this is fragile.
;;
;; This is a table of IP header defaults, combined with the final parts
;; of the code in udp_create and the initial parts of ip_receive.
;;
;; The length field is only a placeholder (actual value set at runtime).
;; The ID field is arbitrary when the Don't Fragment flag is set, according to
;; RFC 6864:
;;
;;   Originating sources MAY set the IPv4 ID field of
;;   atomic datagrams to any value.
;;
;; https://datatracker.ietf.org/doc/html/rfc6864#section-4.1
;; ===========================================================================

ip_header_defaults:                          ;; IP header meaning
                                             ;; -----------------
    ld     b, l                              ;; 0x45: version, IHL
    nop                                      ;; 0x00: DSCP, EN

    ld     hl, #outgoing_header              ;; 2 bytes IP length (placeholder)
    rst    enc28j60_write_memory_small       ;; 2 bytes packet ID (arbitrary)

    ld     b, b                              ;; 0x40: DO NOT FRAGMENT
    nop                                      ;; 0x00: fragment offset 0

    ret                                      ;; 0xC9: time-to-live = 201

    ;; IP header data continues as the first three bytes of ip_receive below

;; ###########################################################################
;; ip_receive
;; ###########################################################################

ip_receive:

    ld    de, #0                             ;; protocol = IP_PROTOCOL_UDP
                                             ;; checksum = 0 (temporary value
                                             ;; for computation)

    ;; -----------------------------------------------------------------------
    ;; clear IP checksum and read a minimal IPv4 header
    ;; -----------------------------------------------------------------------

    ld   (_ip_checksum), de

    ld   e, #IPV4_HEADER_SIZE                ;; D==0 here
    call enc28j60_read_memory_to_rxframe     ;; preserves HL==rx_frame

    ;; -----------------------------------------------------------------------
    ;; Check if a valid IP address has been set, by testing the first octet
    ;; against zero. Set Z as follows:
    ;;
    ;; Z == 0: an IP address has been set, check packet IP address
    ;; Z == 1: no IP address has been set, ignore packet IP address
    ;;
    ;; A == 0 and HL == rx_frame after enc28j60_read_memory_to_rxframe above.
    ;; -----------------------------------------------------------------------

    ld   l, #<outgoing_header + IPV4_HEADER_OFFSETOF_SRC_ADDR
    or   a, (hl)

    ;; -----------------------------------------------------------------------
    ;; This means that once an IP address is set,
    ;; multicasts/broadcasts are ignored.
    ;; -----------------------------------------------------------------------

    ld   de, #rx_frame + IPV4_HEADER_OFFSETOF_DST_ADDR
    call nz, memory_compare_4_bytes
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; Read remaining IP header, skip any options
    ;; -----------------------------------------------------------------------

    ld   a, (rx_frame + IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH)
    add  a, a
    add  a, a              ;; IP version (0x40) shifted out; no masking needed

    push af                               ;; remember IP header size for later

    sub  a, #IPV4_HEADER_SIZE

    ;; -----------------------------------------------------------------------
    ;; Handle IPv4 options, if any. Z==0 means IPv4 options found.
    ;;
    ;; To skip forward past any options, load additional header data
    ;; into UDP part of the buffer (overwritten soon afterwards)
    ;;
    ;; B==0 from enc28j60_read_memory_to_rxframe or memory_compare_4_bytes
    ;; -----------------------------------------------------------------------

    ld   d, b                               ;; D := 0
    ld   e, a                               ;; E := remaining IP header length
    ld   l, #<rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call nz, enc28j60_read_memory

    ;; -----------------------------------------------------------------------
    ;; compute payload length (excluding IP header)
    ;;
    ;; compute DE := HL-B, where
    ;;   DE is the UDP length (host order)
    ;;   HL is the total packet length (network order)
    ;;   B  is the number of bytes currently read (IP header + options)
    ;;
    ;; HL can be any size up to 0x220 bytes, so carry must be considered.
    ;; -----------------------------------------------------------------------

    pop  bc                  ;; B now holds IP header size (including options)

    ld   hl, (rx_frame + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH)
    ld   a, h               ;; HL is in network order, so this is the low byte
    sub  a, b
    ld   e, a
    ld   d, l
    jr   nc, no_carry_in_header_size_subtraction
    dec  d
no_carry_in_header_size_subtraction:

    ;; -----------------------------------------------------------------------
    ;; check IP header checksum
    ;; -----------------------------------------------------------------------

    ld   hl, (_ip_checksum)

    ld   a, h
    and  a, l
    inc  a   ;; if both bytes are 0xff, A will now become zero
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; Check for UDP (everything else will be ignored)
    ;; -----------------------------------------------------------------------

    ld   a, (rx_frame + IPV4_HEADER_OFFSETOF_PROT)
    cp   a, #IP_PROTOCOL_UDP
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; Initialize IP checksum to IP_PROTOCOL_UDP + UDP length (network order)
    ;; for pseudo header. One assumption is made:
    ;;
    ;; The UDP length is assumed to equal IP length - IP header size. This
    ;; _should_ be true in general, but perhaps a creative IP stack could
    ;; break this assumption. It _seems_ to work fine, though...
    ;;
    ;; Compute HL := DE + A, where
    ;;  HL is the IP checksum (network order)
    ;;  DE is the UDP length (host order)
    ;;  A  is IP_PROTOCOL_UDP
    ;;
    ;; DE can be any length, and A is added to the low-order byte,
    ;; so carry must be considered.
    ;; -----------------------------------------------------------------------

    add  a, e
    ld   h, a
    ld   l, d
    jr   nc, no_carry_in_initial_checksum
    inc  l
no_carry_in_initial_checksum:
    ld   (_ip_checksum), hl

    ;; -----------------------------------------------------------------------
    ;; Load UDP payload
    ;; -----------------------------------------------------------------------

    ld   hl, #rx_frame + IPV4_HEADER_SIZE             ;; offset of UDP header
    call enc28j60_read_memory

    ;; -----------------------------------------------------------------------
    ;; Check UDP checksum
    ;; -----------------------------------------------------------------------

    ld   hl, (rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_CHECKSUM)
    ld   a, h
    or   a, l

    ;; UDP checksum is optional: Z flag now set if no UDP checksum given

    ;; Include IPv4 pseudo header in UDP checksum. UDP protocol and length
    ;; were already included (given as initial value above), so we do not add
    ;; it here.

    ld   hl, (_ip_checksum)
    ld   de, #rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    call nz, enc28j60_add_8_bytes_to_checksum_hl
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; Pass on to BOOTP/TFTP
    ;; -----------------------------------------------------------------------

    ld   hl, (rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)

    ;; -----------------------------------------------------------------------
    ;; check low-order byte of port (should always be zero)
    ;; -----------------------------------------------------------------------

    or   a, l      ;; A == 0 from above:
                   ;; either the UDP checksum was zero, A == 0, CALL not taken
                   ;; or add_and_verify_checksum was called, the checksum
                   ;; matched, and so A == 0 was set
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; check high-order byte:
    ;;
    ;; 0x44                -> BOOTP   (UDP_PORT_BOOTP_CLIENT, network order)
    ;; TFTP client port    -> TFTP
    ;; -----------------------------------------------------------------------

    ld   a, h
    cp   a, #UDP_PORT_BOOTP_CLIENT
    jp   z, bootp_receive

    ;; -----------------------------------------------------------------------
    ;; The last sent UDP packet is a TFTP packet, with the selected TFTP
    ;; client port number in the UDP header.
    ;;
    ;; If no TFTP packet has been sent yet, this byte has the value
    ;; UDP_PORT_BOOTP_CLIENT, and would then have been matched above already.
    ;; -----------------------------------------------------------------------

    ld   a, (outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT + 1)
    cp   a, h
    ret  nz

    ;; -----------------------------------------------------------------------
    ;; HANDLE_TFTP_PACKET is a macro, so as to avoid a function call.
    ;;
    ;; B == 0 from enc28j60_read_memory and
    ;; (if invoked) enc28j60_add_8_bytes_to_checksum_hl
    ;; -----------------------------------------------------------------------

    HANDLE_TFTP_PACKET

    ;; -----------------------------------------------------------------------
    ;; HANDLE_TFTP_PACKET returns when done, so no fall-through here
    ;; -----------------------------------------------------------------------


;; ###########################################################################
;; handle_ip_or_arp_packet
;;
;; Helper to select IP or ARP depending on ethertype:
;;   0x0008 -> IP
;;   0x0608 -> ARP
;;
;; ARP is handled directly (inline), jumps to ip_receive for IP.
;;
;; On entry:
;;   A == 0
;;   H == high byte of ethertype (0x00 or 0x06; returns early otherwise)
;; ###########################################################################

handle_ip_or_arp_packet:

    or    a, h
    jp    z, ip_receive
    cp    a, #6
    ret   nz

    ;; =======================================================================
    ;; ARP packet detected: retrieve payload
    ;; =======================================================================

    ld   de, #ARP_IP_ETH_PACKET_SIZE
    call enc28j60_read_memory_to_rxframe

    ;; ------------------------------------------------------------------------
    ;; check header against template
    ;; (ETHERTYPE_IP, ETH_HWTYPE)
    ;; ------------------------------------------------------------------------

    ;; HL is set to rx_frame and preserved by enc28j60_read_memory_to_rxframe

    ld   de, #arp_template
    call memory_compare_4_bytes
    ret  nz                      ;; if not matching the expected header, return

    ;; ------------------------------------------------------------------------
    ;; check the low-order OPER byte, expected to be 1 (REQUEST)
    ;;
    ;; change OPER := REPLY, so the header can be re-used for the response
    ;; ------------------------------------------------------------------------

    ld   l, #<(rx_frame + ARP_OFFSET_OPER + 1)   ;; least significant, network order
    ld   a, (hl)
    inc  (hl)                                                  ;; OPER := REPLY
    dec  a
    ret  nz

    ;; ------------------------------------------------------------------------
    ;; check that the packet was sent to this address
    ;; (assuming TPA is never 0.0.0.0 for an ARP REQUEST)
    ;; ------------------------------------------------------------------------

    ld   l, #<outgoing_header + IPV4_HEADER_OFFSETOF_SRC_ADDR
    ld   de, #rx_frame + ARP_OFFSET_TPA
    call memory_compare_4_bytes
    ret  nz

    ld   bc, #eth_sender_address
    ld   de, #ethertype_arp
    ld   hl, #ENC28J60_TXBUF2_START

    push hl                         ;; stack argument for eth_send_frame below

    call eth_create

    ;; -----------------------------------------------------------------------
    ;; ARP header (copied and modified from the original request)
    ;; -----------------------------------------------------------------------

    ld   hl, #rx_frame
    ld   e, #ARP_OFFSET_SHA                           ;; all 8 bytes up to SHA
    rst  enc28j60_write_memory_small

    ;; -----------------------------------------------------------------------
    ;; SHA: local MAC address
    ;; -----------------------------------------------------------------------

    call enc28j60_write_local_hwaddr

    ;; -----------------------------------------------------------------------
    ;; SPA: local IPv4 address
    ;; -----------------------------------------------------------------------

    ld   e, #IPV4_ADDRESS_SIZE
    ld   hl, #outgoing_header + IPV4_HEADER_OFFSETOF_SRC_ADDR
    rst  enc28j60_write_memory_small

    ;; -----------------------------------------------------------------------
    ;; THA+TPA: remote MAC+IP addresses,
    ;; taken from SHA+SPA fields in source frame
    ;; -----------------------------------------------------------------------

    ld   e, #ETH_ADDRESS_SIZE + IPV4_ADDRESS_SIZE
    ld   l, #<rx_frame + ARP_OFFSET_SHA
    rst  enc28j60_write_memory_small

    ;; -----------------------------------------------------------------------
    ;; ENC28J60_TXBUF2_START is now the top word on the stack,
    ;; as required by eth_send_frame
    ;; -----------------------------------------------------------------------

    ld   hl, #ENC28J60_TXBUF2_START + ETH_HEADER_SIZE + ARP_IP_ETH_PACKET_SIZE

    jr   eth_send_frame


;; ===========================================================================
;; subroutine: reply with ACK
;;
;; requires B == 0 on entry
;; ===========================================================================

tftp_reply_ack:

    ld    hl, (rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)
    ld    (outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl

    ;; ----------------------------------------------------------------------
    ;; no need to update source port here: keep the chosen TFTP client port
    ;; ----------------------------------------------------------------------

    ld    hl, #eth_sender_address

    ;; -----------------------------------------------------------------------
    ;; packet length, network order
    ;; -----------------------------------------------------------------------

    ld    de, #0x0100 * (UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET)

    ;; -----------------------------------------------------------------------
    ;; B == 0 from precondition above
    ;; -----------------------------------------------------------------------

    call  udp_create

    ;; -----------------------------------------------------------------------
    ;; udp_create ends with enc28j60_write_memory, so
    ;;   DE == 0 and
    ;;   HL == outgoing_header + IPV4_HEADER_SIZE + UDP_HEADER_SIZE
    ;;
    ;; Re-use D == 0 and H == 0x5b here.
    ;; -----------------------------------------------------------------------

    ld    e, #TFTP_SIZE_OF_ACK_PACKET
    ld    l, #<rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_OFFSET_OF_OPCODE

    ;; FALL THROUGH to ip_append_data_and_send

;; ###########################################################################
;; ip_append_data_and_send
;;
;; Call enc28j60_write_memory and continue with udp_send.
;; ###########################################################################

ip_append_data_and_send:

    call enc28j60_write_memory

    ;; FALL THROUGH to udp_send


;; ###########################################################################
;; udp_send
;;
;; Send a completed IP/UDP packet (packet length determined by IP header).
;; ###########################################################################

udp_send:

    ld   hl, (outgoing_header + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH)
    ld   a, l  ;; swap byte order in HL
    ld   l, h
    ld   h, a

    ;; -----------------------------------------------------------------------
    ;; Reset retransmission timer. A is not always exactly zero here,
    ;; but close enough (1 for BOOTP BOOTREQUEST, 0 for TFTP).
    ;; -----------------------------------------------------------------------

    ld   (retransmission_count), a

    ;; -----------------------------------------------------------------------
    ;; set DE := start address of frame in transmission buffer,
    ;;     HL := end address of frame in transmission buffer
    ;;
    ;; end address = start
    ;;               + 1 (per-packet control byte)
    ;;               + ETH_HEADER_SIZE
    ;;               + nbr_bytes
    ;;               - 1 (point to last byte, not after it)
    ;;             = start + ETH_HEADER_SIZE + nbr_bytes
    ;;
    ;; Assume that adding ETH_HEADER_SIZE to ENC28J60_TXBUF1_START stays
    ;; in the same RAM page (set in eth.inc).
    ;; -----------------------------------------------------------------------

    ld    de, #ENC28J60_TXBUF1_START+ETH_HEADER_SIZE
    add   hl, de

    ld    e, #<ENC28J60_TXBUF1_START

    push  de                                             ;; push start address

    ;; FALL THROUGH to eth_send_frame


;; ###########################################################################
;; eth_send_frame:
;;
;; Perform a frame transmission.
;; Does not return until the frame has been transmitted.
;;
;; top word on stack:  address of the first byte in the frame
;; HL:                 address of the last byte in the frame
;; ###########################################################################

eth_send_frame:

    ;; -----------------------------------------------------------------------
    ;; set up registers:  ETXST := start address, ETXND := end address
    ;; -----------------------------------------------------------------------

    ld    a, #OPCODE_WCR | ETXNDL
    rst   enc28j60_write_register16

    pop   hl                                              ;; pop start address

    ld    a, #OPCODE_WCR | ETXSTL
    rst  enc28j60_write_register16

    ;; -----------------------------------------------------------------------
    ;; Errata, item 10:
    ;;
    ;; Reset transmit logic before transmitting a frame:
    ;; set bit TXRST in ECON1, then clear it.
    ;;
    ;; Set bank 0 explicitly here (although ECON1 and ESTAT are available in
    ;; every bank 0..3), for the context switch code to access bank 0
    ;; directly.
    ;; -----------------------------------------------------------------------

    ld    e, a                           ;; A == 0, which is the bank of ECON1
    rst   enc28j60_select_bank

    ld    hl, #(0x0100 * ECON1_TXRST)  +  (OPCODE_BFS | ECON1)
    rst   enc28j60_write8plus8

    ;; keep H == ECON1_TXRST
    ld    l, #OPCODE_BFC | ECON1
    rst   enc28j60_write8plus8

    ;; -----------------------------------------------------------------------
    ;; clear ESTAT.TXABRT
    ;; -----------------------------------------------------------------------

    ld    hl, #(0x0100 * ESTAT_TXABRT)  +  (OPCODE_BFC | ESTAT)
    rst   enc28j60_write8plus8

    ;; -----------------------------------------------------------------------
    ;; set ECON1.TXRTS, and poll it until it clears
    ;; -----------------------------------------------------------------------

    ld    hl, #(0x0100 * ECON1_TXRTS)  +  (OPCODE_BFS | ECON1)
    rst   enc28j60_write8plus8

    ld    c, #ECON1

poll_econ1:
    call   enc28j60_read_register

    and    a, h                                 ;; H == ECON1_TXRTS from above
    ret    z

    jr     poll_econ1

;; ---------------------------------------------------------------------------
;; Called by UDP when a BOOTP packet has been received.
;; If a BOOTREPLY with an IP address is found,
;; make a TFTP file read request, otherwise return.
;; ---------------------------------------------------------------------------

tftp_default_file:
    .ascii 'menu.dat'
    .db    0

bootp_receive:

    HANDLE_BOOTP_PACKET

    ;; =======================================================================
    ;; A BOOTREPLY was received
    ;; =======================================================================

    ;; -----------------------------------------------------------------------
    ;; set 'S' (printed below) to bright flashing green/black
    ;; -----------------------------------------------------------------------

    ld   hl, #SERVER_IP_ATTR
    ld   (hl), #BLACK + (GREEN << 3) + BRIGHT + FLASH                  ;; 0xE0

    ;; -----------------------------------------------------------------------
    ;; highlight 'L' (printed below) with bright background
    ;;
    ;; the attribute byte written above happens to be 0xE0 == <LOCAL_IP_ATTR
    ;; -----------------------------------------------------------------------

    ld   l, (hl)                                        ;; HL := LOCAL_IP_ATTR
    ld   (hl), #BLACK + (WHITE << 3) + BRIGHT

    ;; -----------------------------------------------------------------------
    ;; keep configuration for loading 'menu.dat' in DE', HL'
    ;; -----------------------------------------------------------------------

    ld   de, #tftp_default_file                  ;; 'menu.dat'
    ld   hl, #tftp_state_menu_loader             ;; state for loading menu.dat
    exx

    ;; -----------------------------------------------------------------------
    ;; inspect the FILE field, set Z flag if filename is empty
    ;; (interpreted as a request to load 'menu.dat')
    ;; -----------------------------------------------------------------------

    ld   de, #rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_FILE

    ld   a, (de)
    or   a, a
    jr   z, tftp_load_menu_bin

    ;; FALL THROUGH to tftp_request_snapshot


;; ###########################################################################
;; tftp_request_snapshot
;; ###########################################################################

tftp_request_snapshot:

    ;; =======================================================================
    ;; prepare for snapshot loading
    ;; =======================================================================

    ld   hl, #s_header                      ;; state for .z80 snapshot loading

    exx

    ld   hl, #ATTRS_BASE
    ld   de, #ATTRS_BASE + 1
    ld   bc, #0x02E0
    ld   (hl), #WHITE + (WHITE << 3)
    ldir

    ld   c, #0x1f
    ld   (hl), #WHITE + (WHITE << 3) + BRIGHT
    ldir

    xor  a, a
    call show_attr_digit_right

tftp_load_menu_bin:

    exx

    SEND_TFTP_READ_REQUEST

    ;; =======================================================================
    ;; Display IP address information:
    ;;
    ;; print 'L'; local IP address, 'S'; server IP address
    ;;
    ;; This will be displayed when a snapshot is requested too, but remains
    ;; invisible (as PAPER and INK colours have been both set to WHITE+BRIGHT)
    ;; =======================================================================

    ld   de, #LOCAL_IP_POS
    ld   hl, #outgoing_header + IPV4_HEADER_OFFSETOF_SRC_ADDR

    ld   a, #'L'
    call print_char_and_ip_addr

    ld   e, #<SERVER_IP_POS
    ld   a, #'S'

    ;; FALL THROUGH to print_char_and_ip_addr


;; ###########################################################################
;; Subroutine:
;; prints a char and an IP address, four octets of 1-3 digits
;; A = char to print
;; DE = VRAM pointer
;; HL = pointer to IP address
;; AF, AF', and BC are destroyed. DE and HL are increased.
;; ###########################################################################

print_char_and_ip_addr:

    ;; DE = VRAM pointer
    ;; HL = IP address
    ;; AF, BC = scratch

    call  print_char

00001$:
    ld    a, (hl)
    inc   hl

    cp    a, #10           ;; < 10? print only single digit

    call  nc, print_hundreds_and_tens

    call  print_digit      ;; last digit

    ;; -----------------------------------------------------------------------
    ;; Print period or return?
    ;;
    ;; Assume IP addresses to have Z80 addresses divisible by 4
    ;; (set up in globals.inc)
    ;; -----------------------------------------------------------------------

    ld    a, l
    and   a, #3
    ret   z

    ld    a, #'.'
    call  print_char
    jr    00001$           ;; next octet


;; ---------------------------------------------------------------------------
;; Examines A and prints one or two digits.
;;
;; If A >= 100, prints 1 or 2 (hundreds). No 0 will be printed.
;; Then prints tens, unconditionally.
;; Returns with A == (original A) % 10, in range 0..9.
;; ---------------------------------------------------------------------------

print_hundreds_and_tens:

    ld    b, #100
    cp    a, b
    call  nc, print_div    ;; no hundreds? skip entirely, not even a zero

    ld    b, #10

    ;; FALL THROUGH to print_div


;; ---------------------------------------------------------------------------
;; Divides A by B, and prints as one digit. Returns remainder in A.
;; Destroys AF'.
;; ---------------------------------------------------------------------------

print_div:
    call  a_div_b

    ex    af, af'
    ld    a, c

    ;; FALL THROUGH to print_digit


;; ###########################################################################

print_digit:
    add  a, #'0'

    ;; FALL THROUGH to print_char


;; ###########################################################################
;; print_char
;; ###########################################################################

print_char:

    push hl
    push bc

    ld   bc, #(_font_data - 32 * 8)
    ld   l, a
    ld   h, c   ;; 0
    add  hl, hl
    add  hl, hl
    add  hl, hl
    add  hl, bc

    ld   b, #7
    ld   c, d
_print_char_loop:
    inc  hl
    ld   a, (hl)
    ld   (de), a
    inc  d
    djnz _print_char_loop
    ld   d, c

    ex   af, af'            ;;   bring back A after print_div

    inc  e

    pop  bc
    pop  hl

    ret  nz

    ;; -----------------------------------------------------------------------
    ;; E became zero: means we reached the end of one of the 2K VRAM segments.
    ;; Skip to the next one.
    ;;
    ;; This destroys the A value saved from print_div above, but that value
    ;; only matters to print_ip_addr, which never crosses segment boundaries.
    ;; -----------------------------------------------------------------------

    ld   a, d
    add  a, #8
    ld   d, a

    ret