;;
;; Module spi:
;;
;; Definitions for bit-banged SPI access, common to C and assembly files.
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2012-  Patrik Persson & Imrich Kolkol
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
;;/

;; ----------------------------------------------------------------------------
;; Port for SPI communication
;; ----------------------------------------------------------------------------

;; Default SpeccyBoot configuration
SPI_OUT = 0x9f
SPI_IN  = SPI_OUT
SPI_CS  = 0x08

;; TODO: bring this back as a compile-time option
;; Imrich Kolkol's DGBoot maps SPI IN and OUT to different registers
;; SPI_OUT = 0x3f
;; SPI_IN = 0x1f
;; SPI_CS = 0x02

;; ----------------------------------------------------------------------------
;; Masks for the individual bits (also SPI_CS, conditionally selected above)
;; ----------------------------------------------------------------------------

SPI_SCK = 0x01
SPI_RST = 0x40
SPI_MOSI = 0x80

;; ----------------------------------------------------------------------------
;; Strictly not an SPI bit, but seems to belong here anyway:
;; mask for paging out SpeccyBoot (and thus paging in BASIC ROM)
;; ----------------------------------------------------------------------------

PAGE_OUT = 0x20

;; ----------------------------------------------------------------------------
;; SPI idle, MOSI=0, RST high, CS low, SCK low
;; ----------------------------------------------------------------------------

SPI_IDLE = SPI_RST

;; ----------------------------------------------------------------------------
;; macro: read one bit from SPI to accumulator
;; (variants for regular SpeccyBoot and Imrich Konkol's DGBoot)
;; ----------------------------------------------------------------------------

    .macro  spi_read_bit_to_acc
    out   (c), h
    inc   h
    out   (c), h
    dec   h
    in    l, (c)
    rr    a, l
    rla
    .endm

    .macro  spi_read_bit_to_acc_dgboot
    ld    c, #SPI_OUT           ;; added for DGBoot
    out   (c), h
    inc   h
    out   (c), h
    dec   h
    ld    c, #SPI_IN            ;; added for DGBoot
    in    l, (c)
    rr    a, l
    rla
    .endm

;; ----------------------------------------------------------------------------
;; Read 8 bits from SPI to register C.
;; Destroys BC & AF; B will be zero on exit.
;; ----------------------------------------------------------------------------
    .globl _spi_read_byte

;; ----------------------------------------------------------------------------
;; Write 8 bits to SPI from register C.
;; Destroys BC & AF; B will be zero on exit.
;; ----------------------------------------------------------------------------
    .globl _spi_write_byte
