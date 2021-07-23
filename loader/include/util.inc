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

;; ----------------------------------------------------------------------------
;; These MAC address bytes must match the bytes in init.asm
;; ----------------------------------------------------------------------------

MAC_ADDR_0 = 0x4e
MAC_ADDR_1 = 0x23
MAC_ADDR_2 = 0xe7
MAC_ADDR_3 = 0x1b
MAC_ADDR_4 = 0x7a
MAC_ADDR_5 = 0xb3

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

LD_IX_LOW          = 0x2EDD    ;; LD IX-low, #n  (inofficial instruction)
LD_A_IX_LOW        = 0x7DDD    ;; LD A, IX-low   (inofficial instruction)

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
;; attribute VRAM address for kilobyte counter
;; ----------------------------------------------------------------------------

ATTR_DIGIT_ROW = 0x5A00

;; ----------------------------------------------------------------------------
;; codes for fail(), displayed as border colours
;; ----------------------------------------------------------------------------

FATAL_INTERNAL_ERROR      = BLACK
FATAL_VERSION_MISMATCH    = RED
FATAL_FILE_NOT_FOUND      = YELLOW

;; ----------------------------------------------------------------------------
;; codes for warnings
;; ----------------------------------------------------------------------------

WARNING_RETRANSMITTED     = BLUE
WARNING_CHECKSUM_FAILED   = CYAN

;; ----------------------------------------------------------------------------
;; 16-bit word increased by 2 (!) every 20 milliseconds
;; ----------------------------------------------------------------------------

    .globl _timer_tick_count


;; ----------------------------------------------------------------------------
;; Compare memory blocks pointed to by HL and DE. Register B indicates the
;; number of bytes to compare (so, max 256).
;;
;; On return, Z is set if blocks are equal.
;; On a successful compare, B is zero, and HL and DE point to the
;; first byte after the compared blocks.
;; A is the difference in the last comparison (== 0 on success).
;; ----------------------------------------------------------------------------

     .globl memory_compare

     .globl memory_compare_4_bytes          ;; sets B to 4


;; ----------------------------------------------------------------------------
;; Set border to the value of register A, and halt the machine (DI+HALT).
;; Defined in tftp.asm.
;; ----------------------------------------------------------------------------

    .globl fail

;; ----------------------------------------------------------------------------
;; Divides A by B. Returns result in C and remainder in A.
;; C flag is always set. Z flag is always cleared.
;; ----------------------------------------------------------------------------

    .globl a_div_b
    

;; ----------------------------------------------------------------------------
;; print_char
;;
;; A holds character to print
;; DE points to VRAM location
;; destroys AF and AF'; preserves BC, HL; DE is preserved and increased by one
;;
;; Z flag is set if E became zero (that is, after last char in one of the
;; VRAM bitmap thirds).
;;
;; Defined in bootp.asm.
;; ----------------------------------------------------------------------------

    .globl print_char

;; ----------------------------------------------------------------------------
;; show_attr_digit
;;
;; show huge digit in attributes, on row ATTR_DIGIT_ROW and down
;;
;; L: column (0..31)
;; A: digit (0..9), bits 4-7 are ignored
;;
;; Destroys AF, B, DE, HL. Returns with L increased by 7.
;; ----------------------------------------------------------------------------

    .globl show_attr_digit

;; ----------------------------------------------------------------------------
;; alternate entrypoint for show_attr_digit:
;;
;; A: digit (0..9) shifted three bits left, in bits 3..6
;; L: as in show_attr_digit
;; ----------------------------------------------------------------------------

    .globl show_attr_digit_already_shifted

;; ----------------------------------------------------------------------------
;; alternate entrypoint for show_attr_digit:
;;
;; Preserves C. Destroys AF, B, DE, H.
;; H is set to the attribute row for digits (0x5A).
;; L is set to the rightmost position in the digit.
;; B := 0. DE is destroyed (points to font data).
;; ----------------------------------------------------------------------------

    .globl show_attr_digit_right

;; ----------------------------------------------------------------------------
;; title_str
;;
;; The string 'SpeccyBoot <version>', terminated by '.'
;; ----------------------------------------------------------------------------

    .globl title_str

;; ----------------------------------------------------------------------------
;; print_str
;;
;; Prints a string, terminated by '.' (that is, _not_ NUL).
;;
;; The string is truncated to the end of the line, and padded with spaces.
;;
;; HL points to the string to print
;; DE points to VRAM location
;; destroys AF and HL; preserves BC.
;; On return, A==0, Z==1, and DE points to the first character cell on the
;; following line.
;; ----------------------------------------------------------------------------

    .globl print_str