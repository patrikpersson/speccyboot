;;
;; Module spi:
;;
;; Definitions for bit-banged SPI access, common to C and assembly files.
;; NOTE: requires "spi_asm.h" to be included by C preprocessor (for ifdefs).
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2012-  Patrik Persson & Imrich Konkol
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
;;

    .include "platform.inc"

;; ============================================================================
;; Masks for the individual bits (SPI_CS is set in platform.inc)
;; ============================================================================

SPI_SCK  = 0x01
SPI_RST  = 0x40
SPI_MOSI = 0x80

PAGE_OUT = 0x20          ;; mask for paging out SpeccyBoot, paging in BASIC ROM

SPI_IDLE = SPI_RST       ;; SPI idle, MOSI=0, RST high, CS low, SCK low

;; ----------------------------------------------------------------------------
;; Macro: write bit 7 from REG to SPI, then shift REG left one bit.
;; C and Z are cleared after this (as INC A does not generate any carry)
;; ----------------------------------------------------------------------------

    .macro  SPI_WRITE_BIT_FROM   REG

    ld    a, #SPI_IDLE+SPI_IDLE          ;; shifted back left below
    rl    REG
    rra
    out   (SPI_OUT), a
    inc   a
    out   (SPI_OUT), a

    .endm

;; ----------------------------------------------------------------------------
;; Macro: shift REG left one bit, and shift in a bit from SPI to bit 0.
;;        REG can be (HL).
;;
;; Destroys AF
;; ----------------------------------------------------------------------------

    .macro  SPI_READ_BIT_TO   REG

    ld    a, #SPI_IDLE                ;;  7
    out   (SPI_OUT), a                ;; 11
    inc   a                           ;;  4
    out   (SPI_OUT), a                ;; 11
    in    a, (SPI_IN)                 ;; 11
    rra                               ;;  4
    rl    REG                         ;; 15 for REG=(HL), 8 for plain register

    .endm                             ;; 56 or 63 T-states

;; ----------------------------------------------------------------------------
;; rst spi_write_byte
;;
;; Write 8 bits to SPI from register C. Defined in init.asm.
;; Destroys BC & AF; B will be zero on exit. Carry flag is cleared.
;; ----------------------------------------------------------------------------
spi_write_byte = 0x20
