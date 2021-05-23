;; Module util:
;;
;; Various low-level useful stuff.
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

MAC_ADDR_0 = 0xba
MAC_ADDR_1 = 0xdb
MAC_ADDR_2 = 0xad
MAC_ADDR_3 = 0xc0
MAC_ADDR_4 = 0xff
MAC_ADDR_5 = 0xee

TICK_PER_SECOND   = 50


ULA_PORT   = 0xFE

;; ----------------------------------------------------------------------------
;; 128k memory bank configuration
;; ----------------------------------------------------------------------------

MEMCFG_ADDR       = 0x7ffd
MEMCFG_PLUS_ADDR  = 0x1ffd
MEMCFG_ROM_48K    = 0x10
MEMCFG_LOCK       = 0x20

;; ----------------------------------------------------------------------------
;; I/O addresses for sound registers (128k machines)
;; ----------------------------------------------------------------------------
SND_REG_SELECT    = 0xfffd
SND_REG_VALUE     = 0xbffd

    .globl _timer_tick_count


;; ----------------------------------------------------------------------------
;; rst memory_compare
;;
;; Compare memory blocks pointed to by HL and DE. Register B indicates the
;; number of bytes to compare (so, max 256).
;;
;; On return, Z is set if blocks are equal.
;; On a successful compare, B is zero, and HL and DE point to the
;; first byte after the compared blocks.
;; A is destroyed.
;; ----------------------------------------------------------------------------

memory_compare = 0x30


;; ----------------------------------------------------------------------------
;; Set border to the value of register A, and halt the machine (DI+HALT).
;; ----------------------------------------------------------------------------

    .globl fail

;; ----------------------------------------------------------------------------
;; Divides A by B. Returns result in C and remainder in A.
;; C flag is always set. Z flag is always cleared.
;; ----------------------------------------------------------------------------

    .globl a_div_b

;; ----------------------------------------------------------------------------
;; Prints IP address, four octets of 1-3 digits, with a period between.
;; DE = VRAM pointer
;; HL = pointer IP address
;; AF, BC are destroyed. DE and HL are increased.
;; ----------------------------------------------------------------------------

    .globl print_ip_addr


;; ----------------------------------------------------------------------------
;; print_char
;;
;; A holds character to print
;; DE points to VRAM location
;; destroys F; preserves BC, HL; DE is preserved and increased by one
;; ----------------------------------------------------------------------------

    .globl print_char
