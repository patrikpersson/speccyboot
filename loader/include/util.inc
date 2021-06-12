;; Module util:
;;
;; Various low-level useful stuff, including access to
;; ZX Spectrum display, keyboard, and sound.
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

MEMCFG_ADDR         = 0x7ffd
MEMCFG_PLUS_ADDR    = 0x1ffd
MEMCFG_ROM_48K      = 0x10
MEMCFG_LOCK         = 0x20
MEMCFG_PLUS_ROM_48K = 0x04

;; ----------------------------------------------------------------------------
;; I/O addresses for sound registers (128k machines)
;; ----------------------------------------------------------------------------
SND_REG_SELECT    = 0xfffd
SND_REG_VALUE     = 0xbffd

;; ----------------------------------------------------------------------------
;; opcodes for patching code at runtime
;; ----------------------------------------------------------------------------

JR_UNCONDITIONAL   = 0x18      ;; JR offset
JR_NZ              = 0x20      ;; JR NZ, offset
JR_Z               = 0x28      ;; JR Z, offset
JP_UNCONDITIONAL   = 0xc3      ;; JP target
JP_C               = 0xda      ;; JP C, target
JP_NZ              = 0xc2      ;; JP NZ, target
LD_A_N             = 0x3e      ;; LD A, #n
LD_B_N             = 0x06      ;; LD B, #n
LD_BC_NN           = 0x01      ;; LD BC, #nn
LD_DE_NN           = 0x11      ;; LD DE, #nn
LD_HL_NN           = 0x21      ;; LD HL, #nn
LD_IX_NN           = 0x21DD    ;; LD IX, #nn
LD_INDIRECT_HL_N   = 0x36      ;; LD (HL), #n
EI                 = 0xFB      ;; EI

;; ----------------------------------------------------------------------------
;; Spectrum attributes & VRAM addresses
;; ----------------------------------------------------------------------------

BLACK   = 0
BLUE    = 1
RED     = 2
MAGENTA = 3
GREEN   = 4
CYAN    = 5
YELLOW  = 6
WHITE   = 7

BRIGHT  = 0x40
FLASH   = 0x80

;; ----------------------------------------------------------------------------

BITMAP_BASE = 0x4000
ATTRS_BASE  = 0x5800
ROW_LENGTH  = 32

;; ----------------------------------------------------------------------------
;; codes for fatal_error() displayed as border colours
;; ----------------------------------------------------------------------------

FATAL_INTERNAL_ERROR      = BLACK
FATAL_VERSION_MISMATCH    = BLUE
FATAL_NO_RESPONSE         = RED
FATAL_FILE_NOT_FOUND      = YELLOW
FATAL_INCOMPATIBLE        = CYAN
FATAL_INVALID_BOOT_SERVER = MAGENTA

;; ----------------------------------------------------------------------------
;; 16-bit word increased by 2 (!) every 20 milliseconds
;; ----------------------------------------------------------------------------

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
;; Fills (BC-1) bytes with value A, from address HL on.
;; On return, HL points to the last byte in the filled area, and DE=HL+1.
;; BC==0. Z and C flags are unaffected.
;; ----------------------------------------------------------------------------

    .globl fill_memory


;; ----------------------------------------------------------------------------
;; print_char
;;
;; A holds character to print
;; DE points to VRAM location
;; destroys AF and AF'; preserves BC, HL; DE is preserved and increased by one
;;
;; Defined in bootp.asm.
;; ----------------------------------------------------------------------------

    .globl print_char


;; ----------------------------------------------------------------------------
;; print_str
;;
;; Prints a string.
;;
;; If the string resides in snapshot_array (specifically, if H matches that
;; page), then additional checks are made:
;;  - the string is truncated to the end of the line, and padded with spaces.
;;  - the string is terminated by either NUL or '.' (otherwise only NUL)
;;
;; HL points to the string to print
;; DE points to VRAM location
;; destroys F and HL; preserves BC.
;; DE will point to the NUL char, or, if HL is a snapshot_array entry,
;; the first cell of next line.
;; ----------------------------------------------------------------------------

    .globl print_str
