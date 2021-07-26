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
    ;; B   SPI_IDLE+SPI_MOSI
    ;; C   SPI_OUT
    ;; D   SPI_IDLE+SPI_SCK
    ;; E   byte being loaded
    ;; HL  destination in RAM
    ;;
    ;;
    ;; secondary bank (in word_loop)
    ;; -----------------------------
    ;; BC  byte counter
    ;; DE  temp register for one term in sum below
    ;; HL  cumulative 16-bit one-complement sum
    ;;
    ;; F   C flag from previous checksum addition
    ;;

    ld   bc, #0x0100 * (SPI_IDLE + SPI_MOSI) + SPI_OUT
    ld   d, #SPI_IDLE + SPI_SCK

    exx

    ld    c, #OPCODE_RBM
    rst   spi_write_byte

    pop   bc    ;; set BC to byte counter
    inc   b     ;; see spi_read_byte_to_memory below

    ld    hl, (_ip_checksum)

    ;; spi_write_byte clears carry flag, so keep it

    ex    af, af'              ;; to primary AF

    ;; =======================================================================
    ;; each word_loop iteration (16 bits) takes 965.0916 T-states (average)
    ;;   <=> 58.03 kbit/s  (48k machines @3.5MHz)
    ;;       58.80 kbit/s  (128k machines @3.54690MHz)
    ;; =======================================================================

word_loop:

    call spi_read_byte_to_memory      ;; 17+442.0458

    ld   e, a                         ;; 4

    ;; Padding byte handling for odd-sized payloads:
    ;; if this was the last byte, then Z==1,
    ;; B == 0, the CALL NZ below is not taken,
    ;; and D == 0 in the checksum addition instead

    ld   a, b                         ;; 4      D := 0, preserve Z flag

    call nz, spi_read_byte_to_memory  ;; 17+442.0458

    ld   d, a                         ;; 4

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
;; register E. Requires registers to be loaded as follows:
;;
;;   B == SPI_IDLE+SPI_MOSI
;;   C == SPI_OUT
;;   D == SPI_IDLE+SPI_SCK
;;
;; NOTE: the reason for including the bit SPI_MOSI in B is to ensure BC points
;; to a non-contended address (0xc09f). See "Contended Input/Output" in
;; https://worldofspectrum.org/faq/reference/48kreference.htm#Contention:
;;
;;  "The address of the port being accessed is placed on the data bus. If this
;;   is in the range 0x4000 to 0x7fff, the ULA treats this as an attempted
;;   access to contended memory and therefore introduces a delay. If the port
;;   being accessed is between 0xc000 and 0xffff, this effect does not apply,
;;   even on a 128K machine if a contended memory bank is paged into the range
;;   0xc000 to 0xffff."
;;
;; The MOSI bit will be ignored by the ENC28J60 during a read operation
;; (ENC28J60 writes, Spectrum reads). See section 4, figure 4.2 in the ENC28J60
;; data sheet ("Don't Care").
;; ----------------------------------------------------------------------------

   .macro READ_BIT_TO_E

    out   (c), b         ;; 12
    out   (c), d         ;; 12
    in    a, (SPI_IN)    ;; 11
    rra                  ;;  4
    rl    e              ;;  8,   total 47

   .endm

;; ----------------------------------------------------------------------------
;; Subroutine: read one byte. Call with secondary bank selected.
;;
;; The byte is stored in (primary HL), primary E, and A.
;;
;; Primary HL is increased, primary BC decreased, and the secondary bank
;; selected again on exit.
;;
;; Sets Z flag if primary BC == 0.
;; ----------------------------------------------------------------------------

spi_read_byte_to_memory:

    exx                               ;;  4

    ld   e, #1                        ;;  7
spi_byte_inline_loop:
    READ_BIT_TO_E
    READ_BIT_TO_E
    READ_BIT_TO_E
    READ_BIT_TO_E                     ;; 376  (47 * 8)
    jr   nc, spi_byte_inline_loop     ;; 12 + 7

    ld   (hl), e                      ;;  7
    inc  hl                           ;;  6

    ld   a, e                         ;;  4

    exx                               ;;  4

    ;; -------------------------------------------------------------
    ;; The following is equivalent to
    ;;   DEC BC
    ;;   + setting Z flag when BC reaches zero, without involving A
    ;;
    ;; BUT requires B to be increased by 1 in advance, so a length
    ;; of 0x1234 is represented as B==0x13, C==0x34.
    ;; -------------------------------------------------------------

    dec  c                            ;;  4
    ret  nz                           ;;  5   (11 if return taken)
    dec  b                            ;;  4

    ret                               ;; 10
                                      ;; ---
                                      ;; 442 T-states (..RET NZ)
                                      ;; 450 T-states (..DEC B; RET)

    ;; -------------------------------------------------------------
    ;; T-state calculation, assuming a fully loaded TFTP packet:
    ;; 8 (UDP header) + 4 (TFTP header) + 512 (TFTP payload) == 524
    ;;
    ;; Results in 3 long instances (450) and 521 short ones (442),
    ;; so (3*450+521*442)/524 == 442.0458 T-states (average)
    ;; -------------------------------------------------------------


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
;; Execution time: 56*B + 13 *(B-1) + 8 + 10 = 69*B + 5
;; ===========================================================================

read_bits_to_c:

    SPI_READ_BIT_TO   c
    djnz  read_bits_to_c
    ret