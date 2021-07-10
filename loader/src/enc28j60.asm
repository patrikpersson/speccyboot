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

    .module enc28j60

    .include "enc28j60.inc"

    .include "eth.inc"
    .include "spi.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================

    .area _CODE

;; ############################################################################
;; enc28j60_read_memory_to_rxframe
;; ############################################################################

enc28j60_read_memory_to_rxframe:

    ld    hl, #_rx_frame

    ;; FALL THROUGH to enc28j60_read_memory


;; ############################################################################
;; enc28j60_read_memory
;; ############################################################################

enc28j60_read_memory:

    push   hl                      ;; preserve HL
    push   de                      ;; keep byte counter, popped into BC' below

    ;;
    ;; register allocation:
    ;;
    ;;
    ;; primary bank (in spi_read_byte_to_memory)
    ;; -----------------------------------------
    ;; B   byte being loaded, using carry to check for end of loop
    ;; C   SPI_OUT
    ;; D   SPI_IDLE+SPI_SCK
    ;; E   SPI_IDLE
    ;; HL  destination in RAM
    ;;
    ;;
    ;; secondary bank (in word_loop)
    ;; -----------------------------
    ;; BC  byte counter
    ;; D   return value from spi_read_byte_to_memory
    ;; DE  temp register for one term in sum below
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    ld   c, #SPI_OUT
    ld   de, #0x0100 * (SPI_IDLE + SPI_SCK) + SPI_IDLE

    exx

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    pop   bc    ;; set BC to byte counter

    ld    hl, (_ip_checksum)

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; =======================================================================
    ;; Each iteration (16 bits) takes 1041 T-states <=> 53.76 kbit/s. This
    ;; seems to be the sweet spot:
    ;;
    ;;                                            cycle             48kB
    ;; inlining   code footprint (ROM)            delta   bitrate   xfer time
    ;; --------   --------------------            -----   -------   ---------
    ;;      1b     -11B                            +96     49.25     7.98s
    ;;
    ;;      2b         (current)                   (0)     53.76     7.31s
    ;;
    ;;      4b     +22B (2xREAD_BIT_TO_B)          -48     56.49     6.97s
    ;;      8b     +62B (6x -"-, no LD B/JR NC)   -100     59.51     6.61s
    ;;     16b    +153B (approx.; no CALLs)       -147     62.64     6.28s
    ;;
    ;; =======================================================================

word_loop:

    call spi_read_byte_to_memory      ;; 17+479

    ld   e, d                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; B == 0, the CALL NZ below is not taken,
    ;; and D == 0 in the checksum addition instead

    ld   d, b                         ;; 4      D := 0, preserve Z flag

    call nz, spi_read_byte_to_memory  ;; 17+479

    ex   af, af'                      ;; 4
    adc  hl, de                       ;; 15
    ex   af, af'                      ;; 4

    jr   nz, word_loop                ;; 12

    ;; -----------------------------------------------------------------------
    ;; end of payload: add the final carry to HL
    ;; -----------------------------------------------------------------------

    ex    af, af'
    call  add_final_carry_and_store_checksum

    pop   hl                       ;; bring back original HL

do_end_transaction:

    jp   enc28j60_end_transaction_and_return


;; ----------------------------------------------------------------------------
;; Macro for spi_read_byte_to_memory below. Reads one bit from SPI to
;; register B. Requires  C==SPI_OUT,  E==SPI_IDLE  and  D==SPI_IDLE+SPI_SCK.
;; ----------------------------------------------------------------------------

   .macro READ_BIT_TO_B

    out   (c), e         ;; 12
    out   (c), d         ;; 12
    in    a, (SPI_IN)    ;; 11
    rra                  ;;  4
    rl    b              ;;  8,   total 47

   .endm

;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (primary HL), primary B, and secondary D.
;;
;; Primary HL is increased, primary BC decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary BC == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    exx                               ;;  4

    ;; load byte into B; use carry flag as sentinel

    ld    b, #1                       ;;  7
byte_read_loop:
    READ_BIT_TO_B
    READ_BIT_TO_B                     ;; 376  (47 * 8)
    jr    nc, byte_read_loop          ;; 43   (12 * 3 + 7)

    ld   (hl), b                      ;;  7
    inc  hl                           ;;  6

    ld   a, b                         ;;  4

    exx                               ;;  4

    ld   d, a                         ;;  4

    dec  bc                           ;;  6
    ld   a, b                         ;;  4
    or   c                            ;;  4

    ret                               ;; 10

                                      ;; 479 T-states


;; ############################################################################
;; enc28j60_add_to_checksum
;; ############################################################################

enc28j60_add_to_checksum:

    ld    hl, (_ip_checksum)

enc28j60_add_to_checksum_hl:

    or    a, a         ;; clear addition carry

checksum_loop:

    ld    a, (de)
    adc   a, l
    ld    l, a
    inc   de
    ld    a, (de)
    adc   a, h
    ld    h, a
    inc   de

    djnz checksum_loop

    ;; ------------------------------------------------------------------------
    ;; useful subroutine for enc28j60_read_memory above; requires B==0
    ;; ------------------------------------------------------------------------

add_final_carry_and_store_checksum:

    ld    c, b      ;; BC is now zero
    adc   hl, bc    ;; final carry only (BC is zero here)

    ld    (_ip_checksum), hl

    ret


;; ############################################################################
;; enc28j60_read_register
;; ############################################################################

enc28j60_read_register:

    ;; ------------------------------------------------------------------------
    ;; start transaction: RCR = 0x00
    ;; ------------------------------------------------------------------------

    ld    c, e
    rst   spi_write_byte

    ;; ------------------------------------------------------------------------
    ;; Set B to either 8 or 16, since reading MAC and MII registers requires
    ;; ignoring a dummy byte
    ;; ------------------------------------------------------------------------

    ld   b, d
    call read_bits_to_c

    jr   do_end_transaction


;; ############################################################################
;; spi_read_byte_to_c
;; ############################################################################

spi_read_byte_to_c:
    ld   b, #8

    ;; FALL THROUGH to read_bits_to_c

;; ===========================================================================
;; helper: read_bits_to_c
;;
;; Reads B (typically 8) bits into C. Destroys AF, returns with B==0.
;; ===========================================================================

read_bits_to_c:

    SPI_READ_BIT_TO   c
    djnz  read_bits_to_c
    ret