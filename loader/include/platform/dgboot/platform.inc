;;
;; Module platform:
;;
;; platform-specific details for DGBoot (as opposed to SpeccyBoot)
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

;; ============================================================================
;; I/O details for SPI communication
;; ============================================================================

SPI_OUT = 0x3f
SPI_IN  = 0x1f

SPI_CS  = 0x02

;; ============================================================================
;; initialization (assumed to be exactly 12 bytes)
;; ============================================================================

    .macro  platform_init

    ;; Initialize the Didaktik 8255
    CWR   = 0x7f

    ld    a, #0x20
    out   (SPI_OUT),a          ;; Question: why this needed? Pages out DGBoot???
    ld    a, #0x90             ;; PB out, PC out, PA in, mode 0
    out   (CWR), a

    nop                        ;; ensure size is exactly 12 bytes
    nop
    nop
    nop

    .endm
