;; Module enc28j60:
;;
;; Access to the Microchip ENC28J60 Ethernet host.
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

;; ===========================================================================
;; register definitions
;; ===========================================================================

;; ---------------------------------------------------------------------------
;; bank 0
;; ---------------------------------------------------------------------------

ERDPTL                 = 0x00
ERDPTH                 = 0x01
EWRPTL                 = 0x02
EWRPTH                 = 0x03
ETXSTL                 = 0x04
ETXSTH                 = 0x05
ETXNDL                 = 0x06
ETXNDH                 = 0x07
ERXSTL                 = 0x08
ERXSTH                 = 0x09
ERXNDL                 = 0x0a
ERXNDH                 = 0x0b
ERXRDPTL               = 0x0c
ERXRDPTH               = 0x0d
ERXWRPTL               = 0x0e
ERXWRPTH               = 0x0f
ESTAT                  = 0x1d
ECON2                  = 0x1e
ECON1                  = 0x1f

;; ---------------------------------------------------------------------------
;; bank 1
;; ---------------------------------------------------------------------------

EPKTCNT                = 0x19

;; ---------------------------------------------------------------------------
;; bank 2
;; ---------------------------------------------------------------------------

MACON1                 = 0x00                                       ;; MAC/MII
MACON3                 = 0x02                                       ;; MAC/MII
MACON4                 = 0x03                                       ;; MAC/MII
MABBIPG                = 0x04                                       ;; MAC/MII
MAIPGL                 = 0x06                                       ;; MAC/MII
MAIPGH                 = 0x07                                       ;; MAC/MII
MAMXFLL                = 0x0a                                       ;; MAC/MII
MAMXFLH                = 0x0b                                       ;; MAC/MII
MICMD                  = 0x12                                       ;; MAC/MII
MIREGADR               = 0x14                                       ;; MAC/MII
MIWRL                  = 0x16                                       ;; MAC/MII
MIWRH                  = 0x17                                       ;; MAC/MII
MIRDH                  = 0x19                                       ;; MAC/MII

;; ---------------------------------------------------------------------------
;; bank 3
;; ---------------------------------------------------------------------------

MAADR5                 = 0x00                                       ;; MAC/MII
MAADR6                 = 0x01                                       ;; MAC/MII
MAADR3                 = 0x02                                       ;; MAC/MII
MAADR4                 = 0x03                                       ;; MAC/MII
MAADR1                 = 0x04                                       ;; MAC/MII
MAADR2                 = 0x05                                       ;; MAC/MII

;; ----------------------------------------------------------------------------
;; ENC28J60 PHY control registers  (datasheet table 3-3)
;; ----------------------------------------------------------------------------

PHCON1                 = 0x00
PHSTAT2                = 0x11

;; ----------------------------------------------------------------------------
;; Individual bits in ETH, MAC, MII registers
;; ----------------------------------------------------------------------------

ESTAT_CLKRDY           = 0x01
ESTAT_TXABRT           = 0x02
ECON2_PKTDEC           = 0x40
ECON1_TXRST            = 0x80
ECON1_TXRTS            = 0x08
ECON1_RXEN             = 0x04

ERXFCON_CRCEN          = 0x20

MACON1_MARXEN          = 0x01
MACON3_TXCRCEN         = 0x10
MACON4_DEFER           = 0x40
MICMD_MIISCAN          = 0x02

;; ----------------------------------------------------------------------------
;; Bit in high byte of 16-bit PHY register PHSTAT2
;; ----------------------------------------------------------------------------
PHSTAT2_HI_LSTAT       = 0x04

;; ----------------------------------------------------------------------------
;; Opcodes for SPI commands
;; ----------------------------------------------------------------------------

OPCODE_RCR             = 0x00                    ;; OR'ed with register 0..0x1f
OPCODE_WCR             = 0x40                    ;; OR'ed with register 0..0x1f
OPCODE_BFS             = 0x80                    ;; OR'ed with register 0..0x1f
OPCODE_BFC             = 0xA0                    ;; OR'ed with register 0..0x1f
OPCODE_RBM             = 0x3A
OPCODE_WBM             = 0x7A

;; ----------------------------------------------------------------------------
;; Size of receive buffer:
;; IPV4_HEADER_SIZE
;;  + UDP_HEADER_SIZE
;;  + TFTP_HEADER_SIZE
;;  + TFTP_DATA_MAXSIZE
;; ----------------------------------------------------------------------------

RX_FRAME_SIZE = 20 + 8 + 4 + 512

;; ----------------------------------------------------------------------------
;; rst enc28j60_write_register16:
;;
;; Write a 16-bit register.
;;
;; Call with A set to a WCR opcode for the low half of the register,
;; e.g., OPCODE_WCR | ERXRDPTL, and HL set to the 16-bit value to write.
;; Defined in init.asm.
;;
;; Destroys F, BC, DE, L.
;;
;; On exit:
;;   D == original L
;;   E == original A
;;   H == original H (preserved)
;;   L == original A + 1
;;   
;; ----------------------------------------------------------------------------

enc28j60_write_register16 = 0x08

;; ----------------------------------------------------------------------------
;; Ensure that the bank in register E (0..3) is paged in.
;;
;; Destroys AF, BC, HL.
;; ----------------------------------------------------------------------------

enc28j60_select_bank = 0x10

;; ----------------------------------------------------------------------------
;; Write an 8-bit opcode plus another 8-bit value. Defined in init.asm.
;;
;; L holds opcode, H holds the value.
;; Destroys BC, AF. Returns with B == 0.
;; ----------------------------------------------------------------------------

enc28j60_write8plus8 = 0x18

;; ----------------------------------------------------------------------------
;; Read value of indicated ETH/MAC/MII register.
;;
;; On entry:
;;   E: register (0..0x1f) to read
;;   D: 8 for an ETH register, 16 for MAC/MII
;;
;; Returns the read value in A.
;; Destroys AF, BC. B will be zero on exit.
;; ----------------------------------------------------------------------------

READ_ETH_REGISTER     = (8 << 8)
READ_MAC_MII_REGISTER = (16 << 8)

    .globl enc28j60_read_register

;; ----------------------------------------------------------------------------
;; Read a number of bytes from on-chip SRAM, continuing from previous read.
;; The checksum is updated automatically.
;;
;; Call with
;; HL = destination address (buffer to read data into)
;; DE = number of bytes to read
;;
;; HL is preserved. Destroys AF, BC, DE, AF', BC', DE', HL'.
;; Returns A == BC == 0.
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_memory

;; ----------------------------------------------------------------------------
;; Additional entrypoint to enc28j60_read_memory, with HL set to _rx_frame.
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_memory_to_rxframe


;; ----------------------------------------------------------------------------
;; Add a number of 16-bit words to an IP-style checksum.
;;
;; On entry:
;;   DE = pointer to 16-words
;;   HL = current checksum
;;   B = number of 16-bit words (!) to add
;;
;; On exit:
;;   BC == 0
;;   DE points to next byte after checksummed data
;;   HL holds current checksum (also stored in _ip_checksum)
;;   A  == 0 if checksum == 0xffff
;;   Z is set if checksum == 0xffff
;; ----------------------------------------------------------------------------

    .globl enc28j60_add_to_checksum_hl

;; ---------------------------------------------------------------------------
;; Alternate entrypoint to enc28j60_add_to_checksum_hl.
;; Sets B := 4.
;; ---------------------------------------------------------------------------

    .globl enc28j60_add_8_bytes_to_checksum_hl

;; ----------------------------------------------------------------------------
;; Reads one byte (8 bits) from SPI to register C.
;; Destroys AF, B. B==0 on exit.
;; ----------------------------------------------------------------------------

    .globl spi_read_byte_to_c


;; ----------------------------------------------------------------------------
;; Write a number of bytes to on-chip SRAM, continuing after previous write.
;; Defined in init.asm.
;;
;; Call with HL=pointer to data, DE=length of data. Destroys AF, BC.
;;
;; On exit, DE==0, and HL points to the next byte after the written data.
;; Z flag is cleared.
;; ----------------------------------------------------------------------------

    .globl enc28j60_write_memory
    

;; ----------------------------------------------------------------------------
;; Write six bytes of the local MAC address to on-chip SRAM, continuing after
;; previous write. (Shortcut to enc28j60_write_memory, with HL set to
;; eth_local_address and DE set to 6.)
;;
;; Defined in init.asm.
;;
;; Destroys AF, BC, DE, HL.
;;
;; On exit, DE==0, and HL points to the next byte after eth_local_address.
;; Z flag is cleared.
;; ----------------------------------------------------------------------------

    .globl enc28j60_write_local_hwaddr


;; ----------------------------------------------------------------------------
;; rst enc28j60_write_memory_small
;;
;; Shortcut to enc28j60_write_memory, for 8-bit lengths. Defined in init.asm.
;; Same registers are used as for enc28j60_write_memory, except
;;
;;   E = length of data.      D will be set to zero.
;;
;; Call with HL=pointer to data, E=length of data. Destroys AF, BC, DE.
;;
;; On exit, DE==0, and HL points to the next byte after the written data.
;; Z flag is cleared.
;; ----------------------------------------------------------------------------

enc28j60_write_memory_small = 0x28

;; ----------------------------------------------------------------------------
;; rst enc28j60_write_memory_inline
;;
;; Another entrypoint for enc28j60_write_memory, for inline data of 8-bit
;; lengths.
;;
;; The data to send is read from after the RST call site, e.g.,
;;    RST  enc28j60_write_memory_inline
;;    .db  3        ;; number of bytes
;;    .db  x, y, z  ;; inline data
;;    ;; execution continues here
;;
;; Destroys AF, BC, DE, HL.
;;
;; On exit, DE==0, and HL points to the next byte after the written data.
;; Z flag is cleared.
;; ----------------------------------------------------------------------------

enc28j60_write_memory_inline = 0x30

;; ----------------------------------------------------------------------------
;; Helper:
;;
;; end SPI transaction (defined in init.asm)
;; ----------------------------------------------------------------------------

    .globl enc28j60_end_transaction_and_return

