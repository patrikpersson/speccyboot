;;
;; Module platform:
;;
;; platform-specific details for SpeccyBoot (as opposed to DGBoot)
;;
;; Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
;;
;; ----------------------------------------------------------------------------
;;
;; Copyright (c) 2012-  Patrik Persson
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

SPI_OUT = 0x9f
SPI_IN  = SPI_OUT

SPI_CS  = 0x08

;; ============================================================================
;; initialization (assumed to be exactly 12 bytes)
;; ============================================================================

    .macro  platform_init

    ;; before the 128k memory configuration is set (0x7ffd), set the
    ;; +2A/+3 memory configuration (0x1ffd). On a plain 128k machine,
    ;; the access to 0x1ffd would be mapped to 0x7ffd, overwriting the
    ;; 128k configuration. On a 48k machine neither access has any effect.

    ;; Set the ROM selection bit in both registers to page in the 48k
    ;; BASIC ROM (ROM1 on the 128k, ROM3 on +2A/+3).

    ;; https://worldofspectrum.org/faq/reference/128kreference.htm

    ld    de, #((MEMCFG_PLUS_ROM_48K) << 8) + (MEMCFG_ROM_48K)
    ld    bc, #MEMCFG_PLUS_ADDR
    out   (c), d   ;; MEMCFG_PLUS_ROM_48K
    ld    b, #>MEMCFG_ADDR
    out   (c), e   ;; MEMCFG_ROM_48K

    .endm
