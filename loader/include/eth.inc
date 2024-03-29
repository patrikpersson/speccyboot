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

;; ---------------------------------------------------------------------------
;; MEMORY MAP
;; ==========
;;
;; Errata for silicon rev. B5, item #3: receive buffer must start at 0x0000
;;
;; 0x0000   ... 0xXXXX  Receive buffer (FIFO, about 5.5K): automatically
;;                      filled with received packets by the ENC28J60. The host
;;                      updates ERXRDPT to inform the ENC28J60 when data is
;;                      consumed.
;;
;; 0xXXXX+1 ... 0xYYYY  TX buffer 1: BOOTP/TFTP frames. Re-sent on time-out.
;;
;; 0xYYYY+1 ... 0x17FF  TX buffer 2: ARP frames, no reply expected,
;;                                   never re-sent.
;;
;; 0x1800   ... 0x1FFF  Reserved for temporary storage during snapshot
;;                      loading (see context_switch.asm)
;; ---------------------------------------------------------------------------

ENC28J60_RXBUF_START    = 0x0000
ENC28J60_EVACUATED_DATA = 0x1800

;; ---------------------------------------------------------------------------
;; Worst-case payload for transmitted UDP frames (BOOTP REQUEST):
;;
;;      20b  IP header
;;       8b  UDP header
;;     300b  BOOTP packet
;;     ----
;;     328b
;;
;; Worst-case payload for transmitted ARP frames (ARP REPLY):
;;
;;     28b  ARP
;; ---------------------------------------------------------------------------

ETH_MAX_UDP_TX_PAYLOAD = 328
ETH_MAX_ARP_TX_PAYLOAD = 28

;; ---------------------------------------------------------------------------
;; Worst-case payload for received frames:
;;  60   (max IP header size)
;;   8   (UDP header)
;; 516   (TFTP: 4 bytes header, 512 bytes data)
;; ---
;; 574 bytes
;; ---------------------------------------------------------------------------

ETH_MAX_RX_FRAME_SIZE = ETH_HEADER_SIZE + 574

;; ---------------------------------------------------------------------------
;; Transmission buffer sizes:
;; Ethernet header, payload, and 8 bytes of administrative info stored
;; by controller
;;
;; NOTE: ENC28J60_TXBUF1_START is assumed (in stack.asm, ip_send_critical) to
;;       have its first ETH_HEADER_SIZE bytes in the same RAM page.
;; ---------------------------------------------------------------------------

ENC28J60_UDP_TXBUF_SIZE = (ETH_HEADER_SIZE + ETH_MAX_UDP_TX_PAYLOAD + 8)    ;; 0x156
ENC28J60_ARP_TXBUF_SIZE = (ETH_HEADER_SIZE + ETH_MAX_ARP_TX_PAYLOAD + 8)    ;; 0x2a
ENC28J60_TXBUF2_START = (ENC28J60_EVACUATED_DATA - ENC28J60_ARP_TXBUF_SIZE) ;; 0x17d6
ENC28J60_TXBUF1_START = (ENC28J60_TXBUF2_START - ENC28J60_UDP_TXBUF_SIZE)   ;; 0x1680
ENC28J60_RXBUF_END = (ENC28J60_TXBUF1_START - 1)                            ;; 0x167f

ETH_HWTYPE = 1    ;; Ethernet HW type (as used by, e.g., ARP)

ETH_SIZEOF_ETHERTYPE = 2

;; Two classes of frames:
;;
;; PRIORITY       Automatically re-transmitted when a timer expires. If
;;                another PRIORITY frame is transmitted, the timer is reset.
;;
;;                This means frames where we care about an answer
;;                (BOOTP, TFTP).
;;
;; OPTIONAL       Not automatically re-transmitted. The timer is not affected
;;                in any way.
;;
;;                This means frames where we do NOT care about an answer
;;                (ARP replies).
;;
;; NOTE: the frame class value actually maps directly to a transmission buffer
;;       address.

ETH_FRAME_PRIORITY = ENC28J60_TXBUF1_START        ;; bit 8 cleared -- see above
ETH_FRAME_OPTIONAL = ENC28J60_TXBUF2_START        ;; bit 8 set -- see above

;; ----------------------------------------------------------------------------
;; Ethernet addresses
;; ----------------------------------------------------------------------------

    .globl eth_broadcast_address         ;; ff:ff:ff:ff:ff:ff
    .globl eth_local_address             ;; local MAC address
    .globl eth_sender_address            ;; MAC address of last received frame

ETH_ADDRESS_SIZE = 6

ETH_HEADER_SIZE = 14

;; ----------------------------------------------------------------------------
;; useful constant value for eth (defined in menu.asm)
;; ----------------------------------------------------------------------------

    .globl ethertype_arp

;; ============================================================================
;; Initialize Ethernet layer
;; ============================================================================
    .globl eth_init

;; ============================================================================
;; Create an Ethernet frame for transmission. If a previous frame is
;; currently in transmission, block until that is done first.
;;
;; BC: destination MAC address
;; DE: pointer to Ethertype (2 bytes)
;; HL: ENC28J60_TXBUF1_START for IP, ENC28J60_TXBUF1_START for ARP
;; ============================================================================
    .globl eth_create

;; ============================================================================
;; main loop
;; ============================================================================
    .globl main_loop
