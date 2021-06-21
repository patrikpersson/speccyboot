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

;; ----------------------------------------------------------------------------
;; To save some space, information for each register (which bank it belongs
;; to, and whether it is an ETH or a MAC/MII register) is encoded into a single
;; byte, along with the register number.
;;
;; bit 0..4: register number (0..0x1f)
;; bit 5..6: bank (0..3)
;; bit 7:    1 for MAC/MII register, 0 for ETH
;; ----------------------------------------------------------------------------

BANK0          = 0x00
BANK1          = 0x20
BANK2          = 0x40
BANK3          = 0x60

MAC_MII        = 0x80

REG_MASK       = 0x1f
BANK_MASK      = 0x60

;; ----------------------------------------------------------------------------
;; register definitions
;; ----------------------------------------------------------------------------

ERDPTL                = BANK0 | 0x00
ERDPTH                = BANK0 | 0x01
EWRPTL                = BANK0 | 0x02
EWRPTH                = BANK0 | 0x03
ETXSTL                = BANK0 | 0x04
ETXSTH                = BANK0 | 0x05
ETXNDL                = BANK0 | 0x06
ETXNDH                = BANK0 | 0x07
ERXSTL                = BANK0 | 0x08
ERXSTH                = BANK0 | 0x09
ERXNDL                = BANK0 | 0x0a
ERXNDH                = BANK0 | 0x0b
ERXRDPTL              = BANK0 | 0x0c
ERXRDPTH              = BANK0 | 0x0d
ERXWRPTL              = BANK0 | 0x0e
ERXWRPTH              = BANK0 | 0x0f
EDMASTL               = BANK0 | 0x10
EDMASTH               = BANK0 | 0x11
EDMANDL               = BANK0 | 0x12
EDMANDH               = BANK0 | 0x13
EDMADSTL              = BANK0 | 0x14
EDMADSTH              = BANK0 | 0x15
EDMACSL               = BANK0 | 0x16
EDMACSH               = BANK0 | 0x17
EIE                   = BANK0 | 0x1b
EIR                   = BANK0 | 0x1c
ESTAT                 = BANK0 | 0x1d
ECON2                 = BANK0 | 0x1e
ECON1                 = BANK0 | 0x1f

EHT0                  = BANK1 | 0x00
EHT1                  = BANK1 | 0x01
EHT2                  = BANK1 | 0x02
EHT3                  = BANK1 | 0x03
EHT4                  = BANK1 | 0x04
EHT5                  = BANK1 | 0x05
EHT6                  = BANK1 | 0x06
EHT7                  = BANK1 | 0x07
EPMM0                 = BANK1 | 0x08
EPMM1                 = BANK1 | 0x09
EPMM2                 = BANK1 | 0x0a
EPMM3                 = BANK1 | 0x0b
EPMM4                 = BANK1 | 0x0c
EPMM5                 = BANK1 | 0x0d
EPMM6                 = BANK1 | 0x0e
EPMM7                 = BANK1 | 0x0f
EPMCSL                = BANK1 | 0x10
EPMCSH                = BANK1 | 0x11
EPMOL                 = BANK1 | 0x14
EPMOH                 = BANK1 | 0x15
EWOLIE                = BANK1 | 0x16
EWOLIR                = BANK1 | 0x17
ERXFCON               = BANK1 | 0x18
EPKTCNT               = BANK1 | 0x19

MACON1                = BANK2 | MAC_MII | 0x00
MACON3                = BANK2 | MAC_MII | 0x02
MACON4                = BANK2 | MAC_MII | 0x03
MABBIPG               = BANK2 | MAC_MII | 0x04
MAIPGL                = BANK2 | MAC_MII | 0x06
MAIPGH                = BANK2 | MAC_MII | 0x07
MACLCON1              = BANK2 | MAC_MII | 0x08
MACLCON2              = BANK2 | MAC_MII | 0x09
MAMXFLL               = BANK2 | MAC_MII | 0x0a
MAMXFLH               = BANK2 | MAC_MII | 0x0b
MICMD                 = BANK2 | MAC_MII | 0x12
MIREGADR              = BANK2 | MAC_MII | 0x14
MIWRL                 = BANK2 | MAC_MII | 0x16
MIWRH                 = BANK2 | MAC_MII | 0x17
MIRDL                 = BANK2 | MAC_MII | 0x18
MIRDH                 = BANK2 | MAC_MII | 0x19

MAADR5                = BANK3 | MAC_MII | 0x00
MAADR6                = BANK3 | MAC_MII | 0x01
MAADR3                = BANK3 | MAC_MII | 0x02
MAADR4                = BANK3 | MAC_MII | 0x03
MAADR1                = BANK3 | MAC_MII | 0x04
MAADR2                = BANK3 | MAC_MII | 0x05
EBSTSD                = BANK3 | 0x06
EBSTCON               = BANK3 | 0x07
EBSTCSL               = BANK3 | 0x08
EBSTCSH               = BANK3 | 0x09
MISTAT                = BANK3 | MAC_MII | 0x0a
EREVID                = BANK3 | 0x12
ECOCON                = BANK3 | 0x15
EFLOCON               = BANK3 | 0x17
EPAUSL                = BANK3 | 0x18
EPAUSH                = BANK3 | 0x19

;; Special register value address, not used by ENC28J60 (sentinel in table)
ENC28J60_UNUSED_REG   = 0x1a

;; ----------------------------------------------------------------------------
;; ENC28J60 PHY control registers  (datasheet table 3-3)
;; ----------------------------------------------------------------------------

PHCON1                 = 0x00
PHSTAT2                = 0x11

;; ----------------------------------------------------------------------------
;; Individual bits in ETH, MAC, MII registers
;; ----------------------------------------------------------------------------

EIE_TXIE               = 0x08
EIR_TXIF               = 0x08
EIR_TXERIF             = 0x02
ESTAT_CLKRDY           = 0x01
ESTAT_TXABRT           = 0x02
ECON2_PKTDEC           = 0x40
ECON2_AUTOINC          = 0x80
ECON1_TXRST            = 0x80
ECON1_RXRST            = 0x40
ECON1_DMAST            = 0x20
ECON1_CSUMEN           = 0x10
ECON1_TXRTS            = 0x08
ECON1_RXEN             = 0x04

ERXFCON_CRCEN          = 0x20

MACON1_MARXEN          = 0x01
MACON1_RXPAUS          = 0x04
MACON1_TXPAUS          = 0x08
MACON3_TXCRCEN         = 0x10
MACON3_FULDPX          = 0x01
MACON4_DEFER           = 0x40
MICMD_MIISCAN          = 0x02
MISTAT_BUSY            = 0x01
MISTAT_NVALID          = 0x04

;; ----------------------------------------------------------------------------
;; Bit in high byte of 16-bit PHY register PHSTAT2
;; ----------------------------------------------------------------------------
PHSTAT2_HI_LSTAT       = 0x04

;; ----------------------------------------------------------------------------
;; Opcodes for SPI commands
;; ----------------------------------------------------------------------------

OPCODE_RCR             = 0x00   ;; OR'ed with register 0..0x1f
OPCODE_WCR             = 0x40   ;; OR'ed with register 0..0x1f
OPCODE_BFS             = 0x80   ;; OR'ed with register 0..0x1f
OPCODE_BFC             = 0xA0   ;; OR'ed with register 0..0x1f
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
;; buffer for received packet data
;; ----------------------------------------------------------------------------

    .globl _rx_frame

;; ----------------------------------------------------------------------------
;; Useful constant-byte values (used by eth.asm)
;; ----------------------------------------------------------------------------

    .globl ethertype_arp

;; ----------------------------------------------------------------------------
;; rst enc28j60_write_register16:
;;
;; Write a 16-bit register.
;;
;; Call with A set to a WCR opcode for the low half of the register,
;; e.g., OPCODE_WCR + (ERXRDPTL & REG_MASK), and HL set to the 16-bit
;; value to write.
;;
;; Destroys F, BC, DE.
;; ----------------------------------------------------------------------------

enc28j60_write_register16 = 0x08

;; ----------------------------------------------------------------------------
;; Ensure that the bank in register E (0..3) is paged in. Page 0 is
;; always paged in when outside of eth.c.
;;
;; Destroys AF, BC, HL.
;; ----------------------------------------------------------------------------

enc28j60_select_bank = 0x10

;; ----------------------------------------------------------------------------
;; Write an 8-bit opcode plus another 8-bit value. Defined in init.asm.
;;
;; L holds opcode, H holds the value.
;; Destroys BC, AF.
;; ----------------------------------------------------------------------------

enc28j60_write8plus8 = 0x18

;; ----------------------------------------------------------------------------
;; Read value of indicated ETH/MAC/MII register.
;;
;; On entry, E should hold the register descriptor for the register to read.
;; Returns the read value in A.
;; Destroys AF, BC. B will be zero on exit.
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_register

;; ----------------------------------------------------------------------------
;; Poll indicated ETH/MAC/MII register R until
;;
;;   (reg & mask) == expected_value
;;
;; If the condition is not fulfilled within a few seconds,
;; fatal_error() is called.
;;
;; Call with registers:
;;
;; E=reg
;; H=mask
;; L=expected_value
;;
;; Destroys AF, BC
;; ----------------------------------------------------------------------------

    .globl enc28j60_poll_register

;; ----------------------------------------------------------------------------
;; Read a number of bytes from on-chip SRAM, continuing from previous read.
;; The checksum is updated automatically.
;;
;; Call with
;; HL = destination address (buffer to read data into)
;; DE = number of bytes to read
;;
;; Saves HL. Destroys other registers, including alternate ones, except IX/IY.
;; BC == 0 on exit.
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_memory

;; ----------------------------------------------------------------------------
;; Additional entrypoint to enc28j60_read_memory, with HL set to _rx_frame
;; and D set to zero. Reads up to 255 bytes to _rx_frame.
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_memory_to_rxframe

;; ----------------------------------------------------------------------------
;; Additional entrypoint to enc28j60_read_memory, with HL set to _rx_frame
;; and DE set to 20 (useful or both IP header and the ENC28J60 administrative
;; Ethernet header))
;; ----------------------------------------------------------------------------

    .globl enc28j60_read_20b_to_rxframe

;; ----------------------------------------------------------------------------
;; Add a number of 16-bit words to the IP-style checksum.
;;
;; On entry:
;;   DE = pointer to 16-words
;;   B = number of 16-bit words (!) to add
;;
;; On exit:
;;   BC == 0
;;   DE points to next byte after checksummed data
;;   HL holds current checksum (also stored in _ip_checksum)
;; ----------------------------------------------------------------------------

    .globl enc28j60_add_to_checksum


;; ----------------------------------------------------------------------------
;; Like enc28j60_add_to_checksum, but expects the current checksum to be passed
;; in HL (not read from _ip_checksum).
;; ----------------------------------------------------------------------------

    .globl enc28j60_add_to_checksum_hl


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

