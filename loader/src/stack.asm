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

    .module eth

    .include "bootp.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"
    .include "z80_loader.inc"

;; ============================================================================
;; ARP header constants
;; ============================================================================

ARP_OFFSET_SHA =  8         ;; offset of SHA field in ARP header
ARP_OFFSET_SPA = 14         ;; offset of SPA field in ARP header
ARP_OFFSET_TPA = 24         ;; offset of TPA field in ARP header
ARP_IP_ETH_PACKET_SIZE = 28 ;; size of an ARP packet for an IP-Ethernet mapping

;; ----------------------------------------------------------------------------
;; Location of local and server IP addresses (row 23, columns 0 and 16)
;; ----------------------------------------------------------------------------

LOCAL_IP_POS  = (BITMAP_BASE + 0x1100 + 7*32 + 0)
SERVER_IP_POS = (BITMAP_BASE + 0x1100 + 7*32 + 16)

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
_next_frame:
    .ds   2             ;; position of next frame to read from the ENC28J60
    .ds   10
eth_sender_address:
    .ds   ETH_ADDRESS_SIZE
eth_adm_header_ethertype:
    .ds   2

;; ----------------------------------------------------------------------------

_end_of_critical_frame:
    .ds   2                   ;; written to ETXND for re-transmission

;; ============================================================================
;; IP
;; ============================================================================

;; ----------------------------------------------------------------------------
;; IP checksum
;; ----------------------------------------------------------------------------

_ip_checksum:
    .ds   2

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

;; ----------------------------------------------------------------------------
;; high byte of chosen UDP client port
;; (low byte is always 0x45, network order)
;; ----------------------------------------------------------------------------

_tftp_client_port:
    .ds    1

;; ============================================================================

    .area _CODE

;; ############################################################################
;; Main function: initiate BOOTP, receive frames and act on them
;; Must be first in the _CODE segment, as init will execute right into it
;; ############################################################################

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
    
    BOOTP_INIT

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

    ld    de, #(EPKTCNT & REG_MASK) + (8 << 8)    ;; EPKTCNT is an ETH register
    call  enc28j60_read_register

    or    a, a
    jr    nz, main_packet              ;; NZ means a packet has been received

    ;; ------------------------------------------------------------------------
    ;; Re-transmit the last critical frame if timer expired
    ;; ------------------------------------------------------------------------

    ld    a, (_timer_tick_count + 1)   ;; high byte
    or    a, a                         ;; A >= 1 means time-out

    ;; ------------------------------------------------------------------------
    ;; A is presumably 1 == WARNING_RETRANSMITTED here
    ;; ------------------------------------------------------------------------

    ld    hl, (_end_of_critical_frame)
    ld    de, #ENC28J60_TXBUF1_START
    ;; B == 0 from enc28j60_read_register
    call  nz, perform_transmission

    jr    main_loop

main_packet:

    ;; ========================================================================
    ;; done spinning: a packet has been received, bring it into Spectrum RAM
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; set ERDPT (bank 0) to _next_frame
    ;; ------------------------------------------------------------------------

    ld    e, b                            ;; B == 0 from enc28j60_read_register
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

    jr    main_loop


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
    ;; poll ESTAT until ESTAT_CLKRDY is set                        (10+10+17+x)
    ;; ------------------------------------------------------------------------

    ld    de, #(ESTAT & REG_MASK) + (8 << 8)        ;; ESTAT is an ETH register
    ld    hl, #ESTAT_CLKRDY + 0x0100 * ESTAT_CLKRDY
    call  poll_register

    ;; ------------------------------------------------------------------------
    ;; HL is 0x0101 (from poll_register above),
    ;; and needs to be 0x0000 (ENC28J60_RXBUF_START)                   (4+4+16)
    ;; ------------------------------------------------------------------------

    dec   h
    dec   l
    ld    (_next_frame), hl

    ;; ========================================================================
    ;; set up initial register values for ENC28J60                         (10)
    ;; ========================================================================

    ld    hl, #eth_register_defaults

    ;; ------------------------------------------------------------------------
    ;; Data sheet, #11.2:
    ;;
    ;; Wait at least 50us after a System Reset before accessing PHY registers.
    ;;
    ;; 50us == ~178 T-states @ 3.55MHz               (this is the minimum time)
    ;;
    ;; Preamble above is 10+10+17+x+4+4+16+10
    ;;   == 71+x T-states    (where x is the execution time for poll_register)
    ;;
    ;; poll_register includes at least one SPI byte read, which takes at least
    ;; 8*56 T-states, so x >= 448. This poll_register call does not access any
    ;; PHY register.
    ;;
    ;; At least 519 (17+448) T-states, or 146us @3.55MHz, pass from reset until
    ;; the first loop iteration. PHY registers are accessed a few iterations
    ;; into the loop, well after the specified minimum time.
    ;; ------------------------------------------------------------------------

eth_init_registers_loop:

    ld    a, (hl)                               ;; register descriptor, 8 bits
    cp    a, #END_OF_TABLE
    ret   z

    inc   hl

    ;; ------------------------------------------------------------------------
    ;; modify bit 6..7 to OPCODE_WCR (0x40)
    ;; ------------------------------------------------------------------------

    ld    c, a
    set   6, c
    res   7, c

    ld    b, (hl)
    inc   hl

    push  bc                        ;; arguments for enc28j60_write8plus8 below

    ;; ------------------------------------------------------------------------
    ;; select register bank (encoded as bits 6-7 from descriptor)
    ;; ------------------------------------------------------------------------

    exx           ;; keep HL

    rlca          ;; rotate left 2 == rotate right 6
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
    ;; really a bottleneck here. (The bottleneck is instead SPI communication
    ;; from the ENC28J60 to the Spectrum.)
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

    ;; ------------------------------------------------------------------------
    ;; NOTE: no explicit sentinel here. The table is terminated by the byte
    ;; 0xE5, which happens to be the first byte of eth_create below.
    ;; (This instruction has bit 5 set, which none of the table entries have.)
    ;; ------------------------------------------------------------------------
    
END_OF_TABLE = 0xE5                                                  ;; PUSH HL

;; ############################################################################
;; eth_create
;; ############################################################################

eth_create:

    ;; ------------------------------------------------------------------------
    ;; NOTE: the first instruction here (0xE5) terminates the table above.
    ;; ------------------------------------------------------------------------

    push  hl                                                 ;; stack Ethertype
    push  bc                                   ;; stack destination MAC address
    push  de                                       ;; stack transmission buffer

    ;; ------------------------------------------------------------------------
    ;; select default bank for ENC28J60
    ;; ------------------------------------------------------------------------

    ld    e, #0
    rst   enc28j60_select_bank

    ;; ------------------------------------------------------------------------
    ;; set up EWRPT for writing packet data
    ;; ------------------------------------------------------------------------

    ld    a, #OPCODE_WCR + (EWRPTL & REG_MASK)
    pop   hl                                      ;; recall transmission buffer
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

    ld    e, #ETH_ADDRESS_SIZE
    pop   hl                                  ;; recall destination MAC address
    rst   enc28j60_write_memory_small

    ;; ------------------------------------------------------------------------
    ;; write source (local) MAC address
    ;; ------------------------------------------------------------------------

    call  enc28j60_write_local_hwaddr

    ;; ------------------------------------------------------------------------
    ;; write Ethertype
    ;; ------------------------------------------------------------------------

    ld    e, #ETH_SIZEOF_ETHERTYPE
    pop   hl                                                ;; recall Ethertype
    rst   enc28j60_write_memory_small
    ret


;; ############################################################################
;; Create UDP reply to the sender of the received packet currently processed.
;;
;; Call with
;;   DE: number of bytes in payload (NETWORK ORDER)
;; ############################################################################

    .area _CODE

tftp_reply:

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT)
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT), hl

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)
    ld   (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_SRC_PORT), hl

    ld   hl, #eth_sender_address

    ld   bc, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR

    ;; FALL THROUGH to udp_create


;; ############################################################################
;; udp_create
;; ############################################################################

udp_create:

    push  hl
    push  bc

    ;; ----------------------------------------------------------------------
    ;; Set up a header template, to be filled in with proper data below.
    ;; ----------------------------------------------------------------------

    push  de

    ld    hl, #ip_header_defaults
    ld    de, #_header_template
    ld    bc, #12        ;; IP v4 header size excluding src/dst addresses

    ldir

    pop   hl         ;; recall UDP length

    ;; ----------------------------------------------------------------------
    ;; set UDP length (network order)
    ;; ----------------------------------------------------------------------

    ld     (_header_template + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH), hl

    ;; ----------------------------------------------------------------------
    ;; Add IPV4_HEADER_SIZE to HL. This can safely be done as a byte addition
    ;; (no carry needed), as HL has one of the following values:
    ;;
    ;; BOOTP boot request: UDP_HEADER_SIZE + BOOTP_PACKET_SIZE = 308 = 0x134
    ;; TFTP read request: UDP_HEADER_SIZE
    ;;                       + TFTP_SIZE_OF_RRQ_PREFIX
    ;;                       + TFTP_SIZE_OF_RRQ_OPTION
    ;;                    = 27 = 0x1b
    ;; TFTP ACK: UDP_HEADER_SIZE + TFTP_SIZE_OF_ACK_PACKET = 8 + 4 = 0x0c
    ;; TFTP ERROR: UDP_HEADER_SIZE + TFTP_SIZE_OF_ERROR_PACKET = 8 + 5 = 0x0d
    ;;
    ;; In all these cases, the lower byte (that is, H in network order)
    ;; is < 0xfc, so adding IPV4_HEADER_SIZE = 20 = 0x14 as a byte addition
    ;; is safe.
    ;; ----------------------------------------------------------------------

    ld    a, h                    ;; least significant byte in network order
    add   a, #IPV4_HEADER_SIZE
    ld    h, a

    ;; ----------------------------------------------------------------------
    ;; prepare IP header in _header_template
    ;; ----------------------------------------------------------------------

    ld    (_header_template + 2), hl

    ;; copy source IP address

    ld    de, #_header_template + 12   ;; source IP address
    ld    hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    ld    bc, #4
    ldir

    ;; copy destination IP address

    pop   hl
    ld    c, #4       ;; B == 0 after LDIR above
    ;; keep DE from above: destination address follows immediately after source
    ldir

    ;; ----------------------------------------------------------------------
    ;; compute checksum of IP header
    ;; ----------------------------------------------------------------------

    ld     h, b   ;; BC==0 here after LDIR above
    ld     l, c

    ld     b, #(IPV4_HEADER_SIZE / 2)   ;; number of words (10)
    ld     de, #_header_template
    call   enc28j60_add_to_checksum_hl

    ld     a, l
    cpl
    ld     l, a
    ld     a, h
    cpl
    ld     h, a
    ld     (_header_template + IPV4_HEADER_OFFSETOF_CHECKSUM), hl

    ;; ----------------------------------------------------------------------
    ;; create IP packet
    ;; ----------------------------------------------------------------------

    pop    bc             ;; destination MAC address
    ld     de, #ENC28J60_TXBUF1_START
    ld     hl, #ethertype_ip
    call   eth_create

    ;; ----------------------------------------------------------------------
    ;; Write IPv4 + UDP headers. The UDP checksum remains zero here,
    ;; so the UDP checksum is not used in outgoing packets.
    ;; ----------------------------------------------------------------------

    ld     e, #IPV4_HEADER_SIZE + UDP_HEADER_SIZE

    ;; FALL THROUGH and continue execution in the IP header defaults

;; ============================================================================
;; IP header defaults
;; https://en.wikipedia.org/wiki/IPv4#Header
;;
;; Warning: this is fragile.
;;
;; This is a table of IP header defaults, combined with the final parts
;; of the code in udp_create and the initial parts of ip_receive.
;;
;; The length field is only a placeholder (the actual value is set at runtime).
;; The ID field is arbitrary when the Don't Fragment flag is set, according to
;; RFC 6864:
;;
;;   Originating sources MAY set the IPv4 ID field of
;;   atomic datagrams to any value.
;;
;; https://datatracker.ietf.org/doc/html/rfc6864#section-4.1
;; ============================================================================

ip_header_defaults:                          ;; IP header meaning
                                             ;; -----------------
    ld     b, l                              ;; 0x45: version, IHL
    nop                                      ;; 0x00: DSCP, EN

    ld     hl, #_header_template             ;; 2 bytes IP length (placeholder)
    rst    enc28j60_write_memory_small       ;; 2 bytes packet ID (arbitrary)

    ld     b, b                              ;; 0x40: DO NOT FRAGMENT
    nop                                      ;; 0x00: fragment offset 0

    ret                                      ;; 0xC9: time-to-live = 201

    ;; IP header data continues as the first three bytes of ip_receive below

;; ############################################################################
;; ip_receive
;; ############################################################################

ip_receive:

    ld    de, #0                             ;; protocol = IP_PROTOCOL_UDP
                                             ;; checksum = 0 (temporary value
                                             ;; for computation)

    ;; clear IP checksum

    ld   (_ip_checksum), de

    ;; read a minimal IPv4 header

    ld   e, #IPV4_HEADER_SIZE            ;; D==0 here
    call enc28j60_read_memory_to_rxframe  ;; preserves HL==rx_frame

    ;; ------------------------------------------------------------
    ;; Check the IP destination address
    ;; ------------------------------------------------------------

    ;; Check if a valid IP address has been set. Set Z as follows:
    ;;
    ;; Z == 0: an IP address has been set, check packet IP address
    ;; Z == 1: no IP address has been set, ignore packet IP address

    ld   l, #<_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET ;; HL preserved above
    ld   a, (hl)                        ;; a non-zero first octet
    or   a                              ;; means an address has been set

    ;; This means that once an IP address is set,
    ;; multicasts/broadcasts are ignored.

    ld   de, #_rx_frame + IPV4_HEADER_OFFSETOF_DST_ADDR
    call nz, memory_compare_4_bytes
    ret  nz

    ;; ------------------------------------------------------------
    ;; Read remaining IP header, skip any options
    ;; ------------------------------------------------------------

    ;; Read header size

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_VERSION_AND_LENGTH)
    add  a, a
    add  a, a         ;; the IP version is shifted out; no masking needed

    push af     ;; remember IP header size for later

    sub  a, #IPV4_HEADER_SIZE

    ;; -----------------------------------------------------------------------
    ;; Handle IPv4 options, if any. Z==0 means IPv4 options found.
    ;; -----------------------------------------------------------------------

    ;; To skip forward past any options, load additional header data
    ;; into UDP part of the buffer (overwritten soon afterwards)

    ;; B==0 from enc28j60_read_memory_to_rxframe or memory_compare_4_bytes

    ld   d, b    ;; D := 0
    ld   e, a    ;; E := IP header length
    ld   l, #<_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call nz, enc28j60_read_memory

    ;; ------------------------------------------------------------
    ;; compute payload length (excluding IP header)
    ;; ------------------------------------------------------------

    pop  bc          ;; B now holds IP header size

    ;; compute T-N, where
    ;;   T is the total packet length
    ;;   N is the number of bytes currently read (IP header + options)

    ld   hl, (_rx_frame + IPV4_HEADER_OFFSETOF_TOTAL_LENGTH)
    ld   a, h    ;; HL is in network order, so this is the low byte
    sub  a, b
    ld   e, a
    ld   d, l
    jr   nc, no_carry_in_header_size_subtraction
    dec  d
no_carry_in_header_size_subtraction:

    ;; DE now holds UDP length (host order)

    ;; ------------------------------------------------------------
    ;; check IP header checksum
    ;; ------------------------------------------------------------

    call ip_receive_check_checksum

    ;; ------------------------------------------------------------
    ;; Check for UDP (everything else will be ignored)
    ;; ------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_OFFSETOF_PROT)
    cp   a, #IP_PROTOCOL_UDP
    ret  nz

    ;; ------------------------------------------------------------
    ;; Initialize IP checksum to IP_PROTOCOL_UDP + UDP length
    ;; (network order) for pseudo header. One assumption is made:
    ;;
    ;; - The UDP length is assumed to equal IP length - IP header
    ;;   size. This should certainly be true in general, but
    ;;   perhaps a creative IP stack could break this assumption.
    ;;   It _seems_ to work fine, though...
    ;; ------------------------------------------------------------

    add  a, e
    ld   h, a
    ld   l, d
    jr   nc, no_carry
    inc  l
no_carry:
    ld   (_ip_checksum), hl

    ;; ------------------------------------------------------------
    ;; Load UDP payload
    ;; ------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE   ;; offset of UDP header
    call enc28j60_read_memory

    ;; ------------------------------------------------------------
    ;; Check UDP checksum
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_CHECKSUM)
    ld   a, h
    or   a, l

    ;; UDP checksum is optional: Z flag now set if no UDP checksum given

    ;; Include IPv4 pseudo header in UDP checksum. UDP protocol and length
    ;; were already included (given as initial value above), so we do not add
    ;; it here.

    ld   b, #IPV4_ADDRESS_SIZE    ;; number of words (4 for two IP addresses)
    ld   de, #_rx_frame + IPV4_HEADER_OFFSETOF_SRC_ADDR
    call nz, add_and_verify_checksum

    ;; ------------------------------------------------------------
    ;; Pass on to BOOTP/TFTP
    ;; ------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_DST_PORT)

    ;; BOOTP or TFTP response?
    ;;
    ;; for BOOTP, the port has the value 0x4400
    ;; (UDP_PORT_BOOTP_CLIENT, network order)
    ;;
    ;; for TFTP, port has the value 0x45rr, where 'rr' is a random value
    ;; (that is, the high byte is UDP_PORT_TFTP_SERVER, as chosen in tftp.inc)

    ld   a, h
    sub  a, #UDP_PORT_BOOTP_CLIENT
    jp   z, bootp_receive
    dec  a             ;; UDP_PORT_TFTP_SERVER?
    ret  nz
    ld   a, (_tftp_client_port)
    cp   a, l
    ret  nz

    ;; =======================================================================
    ;; A packet for the TFTP client port was received.
    ;; Only accept it if an IP address has been set.
    ;; =======================================================================

    ld   a, (_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET)
    or   a  ;; a non-zero first octet
    ret  z

    ;; -------------------------------------------------------------------
    ;; handle_tftp_packet is a macro, so as to avoid a function call
    ;; -------------------------------------------------------------------

    HANDLE_TFTP_PACKET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


;; ############################################################################
;; tftp_read_request
;; ############################################################################

tftp_read_request:

    PREPARE_TFTP_READ_REQUEST

    ;; FALL THROUGH to ip_send; B==0 from PREPARE_TFTP_READ_REQUEST


;; ############################################################################
;; ip_send
;; ############################################################################

ip_send:

    ld   hl, (_header_template + 2)   ;; IP length
    ld   a, l  ;; swap byte order in HL
    ld   l, h
    ld   h, a

    ld   de, #ENC28J60_TXBUF1_START

    jr   eth_send                        ;; B==0 from PREPARE_TFTP_READ_REQUEST


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
    dec  (hl)
    ret  nz

    ;; ------------------------------------------------------------------------
    ;; check that a local IP address has been set,
    ;; and that the packet was sent to this address
    ;; ------------------------------------------------------------------------

    ;; A is 0 from memory_compare above

    ld   l, #<_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    or   a, (hl)
    ret  z

    ld   de, #_rx_frame + ARP_OFFSET_TPA
    call memory_compare_4_bytes
    ret  nz   ;; if the packet is not for the local IP address, return

    ld   bc, #eth_sender_address
    ld   de, #ENC28J60_TXBUF2_START
    ld   hl, #ethertype_arp
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

    ld   de, #ENC28J60_TXBUF2_START
    ld   hl, #ARP_IP_ETH_PACKET_SIZE

    ;; FALL THROUGH to eth_send; B==0 from enc28j60_write_memory_small


;; ############################################################################
;; eth_send
;;
;; On entry:
;;   DE: start of transmission buffer
;;   HL: number of bytes
;;   B:  0
;; ############################################################################

eth_send:

    ;; ------------------------------------------------------------------------
    ;; set DE := start address of frame in transmission buffer,
    ;;     HL := end address of frame in transmission buffer
    ;;
    ;; end address = start
    ;;               + 1 (per-packet control byte)
    ;;               + ETH_HEADER_SIZE
    ;;               + nbr_bytes
    ;;               - 1 (point to last byte, not after it)
    ;;             = start + ETH_HEADER_SIZE + nbr_bytes
    ;; ------------------------------------------------------------------------

    add   hl, de
    ld    c, #ETH_HEADER_SIZE        ;; B == 0
    add   hl, bc

    ;; ------------------------------------------------------------------------
    ;; Check if DE points to a critical frame (BOOTP/TFTP, not ARP). Only
    ;; need to check bit 8; that is, bit 0 in the high byte (see eth.inc).
    ;; ------------------------------------------------------------------------

    ld    a, #WHITE

    bit   0, d                      ;; refer to address calculations in eth.inc
    jr    nz, perform_transmission

    ;; ------------------------------------------------------------------------
    ;; this is a critical frame: update _end_of_critical_frame
    ;; ------------------------------------------------------------------------

    ld    (_end_of_critical_frame), hl

    ;; FALL THROUGH to perform_transmission


;; ############################################################################
;; perform_transmission:
;;
;; Perform a frame transmission.
;; Does not return until the frame has been transmitted.
;;
;; A: border colour, to indicate regular transmission/retransmission
;; B: must be 0
;; DE: address of the first byte in the frame
;; HL: address of the last byte in the frame
;; ############################################################################

perform_transmission:

    ;; ------------------------------------------------------------------------
    ;; use border color to indicate retransmission status
    ;; ------------------------------------------------------------------------

    out   (ULA_PORT), a

    ;; ----------------------------------------------------------------------
    ;; set up registers:  ETXST := start_address, ETXND := end_address
    ;; ----------------------------------------------------------------------

    push  hl   ;; remember HL=end_address
    push  de

    ld    e, b     ;; B == 0: bank of ETXST and ETXND
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

    ;; B == 0 after enc28j60_write_register16 above

    ld    c, b
    ld    (_timer_tick_count), bc

    ;; ----------------------------------------------------------------------
    ;; Poll for link to come up (if it has not already)
    ;;
    ;; NOTE: this code assumes the MIREGADR/MICMD registers to be configured
    ;;       for continuous scanning of PHSTAT2 -- see eth_init
    ;; ----------------------------------------------------------------------

    ld    e, #2             ;; bank 2 for MIRDH
    rst   enc28j60_select_bank

    ;; poll MIRDH until PHSTAT2_HI_LSTAT is set

    ld    de, #(MIRDH & REG_MASK) + (16 << 8)  ;; MIRDH is a MAC_MII register
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

    ;; keep H == ECON1_TXRST
    ld    l, #OPCODE_BFC + (ECON1 & REG_MASK)
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

    ld    l, b
    ;; keep H==ECON1_TXRTS from above, B==0 from _spi_write_byte

    ;; ----------------------------------------------------------------------
    ;; This is the following instruction:
    ;;
    ;; LD DE, #(ECON1 & REG_MASK) + (8 << 8)    (as ECON1 is an ETH register)
    ;;
    ;; list individual bytes to make the bytes (0x08, 0x06) addressable as
    ;; ethertype_arp
    ;; ----------------------------------------------------------------------

    .db   LD_DE_NN
    .db   (ECON1 & REG_MASK)

  ;; ========================================================================
  ;; ARP Ethertype (0x08 0x06)
  ;; ========================================================================

ethertype_arp:

    .db   0x08

    ;; followed by 0x06 (LD B, #n) below

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
;; DE=register, as for enc28j60_read_register
;; H=mask
;; L=expected_value
;;
;; Destroys AF, BC
;; ----------------------------------------------------------------------------

poll_register:

    ;; Ensure BC is at least 0x5000, give controller plenty of time to respond

    ld     b, #0x50
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

    ;; A is zero here, which is FATAL_INTERNAL_ERROR
    jr     fail


;; ############################################################################
;; tftp_state_menu_loader
;; ############################################################################

tftp_default_file:
    .ascii 'menu.bin'
    .db    0

tftp_state_menu_loader:

    ld  hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    bit 1, b   ;; see below
    ldir
    ld  (_tftp_write_pos), de

    ;; ------------------------------------------------------------------------
    ;; If a full TFTP packet was loaded, return.
    ;; (BC above should be exactly 0x200 for all DATA packets except the last
    ;; one, never larger; so we are done if bit 1 is set in B)
    ;; ------------------------------------------------------------------------

    ret nz

    ;; ========================================================================
    ;; This was the last packet of the stage 2 binary:
    ;; check version signature and run the stage 2 loader
    ;; ========================================================================

    ;; ------------------------------------------------------------------------
    ;; check version signature
    ;; ------------------------------------------------------------------------

    ld   hl, #stage2_start
    ld   a, #VERSION_MAGIC
    cp   a, (hl)

    ;; ------------------------------------------------------------------------
    ;; If the signature matches, launch the menu.bin binary, otherwise fail.
    ;;
    ;; This will start with executing the VERSION_MAGIC byte, which is benign
    ;; (it is a LD r, r' instruction).
    ;; ------------------------------------------------------------------------

    push hl
    ret  z

    ;; ------------------------------------------------------------------------
    ;; display firmware version on screen and fail
    ;; ------------------------------------------------------------------------

    ;; lower 4 bits of A is now the ROM loader (stage 1) version number
    call show_attr_digit_right
    ld   a, #FATAL_VERSION_MISMATCH

    ;; FALL THROUGH to fail

;; ############################################################################
;; fail
;; ############################################################################

fail:

    ;; -----------------------------------------------------------------------
    ;; It would make some sense to RESET the ENC28J60 here. However, any
    ;; outgoing (but not yet transmitted) packets would then be lost, and
    ;; possibly confuse debugging.
    ;; -----------------------------------------------------------------------

    di
    out (ULA_PORT), a
    halt

;; -----------------------------------------------------------------------
;; Subroutine: add a number of bytes to IP checksum,
;; then verify the resulting checksum.
;; -----------------------------------------------------------------------

add_and_verify_checksum:

    call enc28j60_add_to_checksum

    ;; FALL THROUGH to ip_receive_check_checksum

;; -----------------------------------------------------------------------
;; Helper: check IP checksum.
;; If OK (0xffff): return to caller.
;; if not OK: pop return address and return to next caller
;;            (that is, return from ip_receive)
;; Must NOT have anything else on stack when this is called.
;; Destroys AF, HL. Returns with A==0, H==L==0xff and Z set on success.
;; -----------------------------------------------------------------------

ip_receive_check_checksum:
    ld   hl, (_ip_checksum)
    ld   a, h
    and  a, l
    inc  a   ;; if both bytes are 0xff, A will now become zero
    ret  z

    pop  af   ;; pop return address within ip_receive

    ld   a, #WARNING_CHECKSUM_FAILED
    out  (ULA_PORT), a

    ret       ;; return to _caller_ of ip_receive


;; ----------------------------------------------------------------------------
;; Called by UDP when a BOOTP packet has been received.
;; If a BOOTREPLY with an IP address is found,
;; continue with tftp_request_snapshot; otherwise return.
;; ----------------------------------------------------------------------------

bootp_receive:

    ;; ------------------------------------------------------------------------
    ;; Verify that the high-order byte of the port (network order) is zero.
    ;; A is zero on entry, due to the subtraction in the BOOTP/TFTP check above
    ;; ------------------------------------------------------------------------

    or   a, l
    ret  nz

    HANDLE_BOOTP_PACKET

    ;; ========================================================================
    ;; A BOOTREPLY was received: inspect the FILE field
    ;; ========================================================================

    ld   de, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + BOOTP_OFFSETOF_FILE

    ;; FALL THROUGH to tftp_request_snapshot


;; ###########################################################################
;; tftp_request_snapshot
;; ###########################################################################

tftp_request_snapshot:

    ld   hl, #s_header                       ;; state for .z80 snapshot loading

    ;; ------------------------------------------------------------------------
    ;; an empty filename is interpreted as a request to load 'menu.bin'
    ;; ------------------------------------------------------------------------

    ld   a, (de)
    or   a, a
    jr   nz, filename_set

    ;; ------------------------------------------------------------------------
    ;; attributes for 'S' indicator: black ink, green paper, bright, flash
    ;; ------------------------------------------------------------------------

    ld   hl, #ATTRS_BASE + 23 * 32 + 16           ;; (23, 16)
    ld   (hl), #(BLACK | (GREEN << 3) | BRIGHT | FLASH)

    ;; ------------------------------------------------------------------------
    ;; attributes for 'L' indicator: black ink, white paper, bright
    ;; ------------------------------------------------------------------------

    ld   l, (hl)                                  ;; (23, 0)
    ld   (hl), #(BLACK | (WHITE << 3) | BRIGHT)

    ld   hl, #tftp_state_menu_loader              ;; state for loading menu.bin
    ld   de, #tftp_default_file                   ;; 'menu.bin'

filename_set:

    call tftp_read_request

    ;; ========================================================================
    ;; Display IP address information:
    ;;
    ;; print 'L', local IP address, 'S', server IP address
    ;; ========================================================================

    ld    a, #'L'
    ld    de, #LOCAL_IP_POS
    ld    hl, #_ip_config + IP_CONFIG_HOST_ADDRESS_OFFSET
    call print_ip_addr

    ld    a, #'S'
    ld    e, #<SERVER_IP_POS

    ;; FALL THROUGH to print_ip_addr


;; ############################################################################
;; Subroutine:
;; prints IP address, four octets of 1-3 digits, with a descriptive letter
;; ('L' or 'S') and periods between octets.
;; A = initial letter to print ('L' or 'S')
;; DE = VRAM pointer
;; HL = pointer to IP address
;; AF, AF', and BC are destroyed. DE and HL are increased.
;; ############################################################################

print_ip_addr:

    call  print_char             ;; initial letter

    ;; DE = VRAM pointer
    ;; HL = IP address
    ;; AF, BC = scratch

    ld    b, #4       ;; loop counter, four octets

00001$:
    push  bc

    ld    a, (hl)
    inc   hl

    cp    a, #10           ;; < 10? print only single digit

    call  nc, print_hundreds_and_tens

    call  print_digit      ;; last digit

    pop   bc

    ;; print period?
    dec   b
    ret   z

    ld    a, #'.'
    call  print_char
    jr    00001$           ;; next octet


;; ----------------------------------------------------------------------------
;; Examines A and prints one or two digits.
;;
;; If A >= 100, prints 1 or 2 (hundreds). No 0 will be printed.
;; Then prints tens, unconditionally.
;; Returns with A == (original A) % 10, in range 0..9.
;; ----------------------------------------------------------------------------

print_hundreds_and_tens:

    ld    b, #100
    cp    a, b
    call  nc, print_div    ;; no hundreds? skip entirely, not even a zero

    ld    b, #10

    ;; FALL THROUGH to print_div


;; ----------------------------------------------------------------------------
;; Divides A by B, and prints as one digit. Returns remainder in A.
;; Destroys AF'.
;; ----------------------------------------------------------------------------

print_div:
    call  a_div_b

    ex    af, af'
    ld    a, c

    ;; FALL THROUGH to print_digit


;; ############################################################################

print_digit:
    add  a, #'0'

    ;; FALL THROUGH to print_char


;; ############################################################################
;; _print_char
;; ############################################################################

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

    ret
