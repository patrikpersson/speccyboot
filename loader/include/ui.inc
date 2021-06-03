;;
;; Module ui:
;;
;; Access to ZX Spectrum display, keyboard, and sound.
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
FATAL_NO_SNAPSHOTS        = GREEN
FATAL_FILE_NOT_FOUND      = YELLOW
FATAL_INCOMPATIBLE        = CYAN
FATAL_INVALID_BOOT_SERVER = MAGENTA

;; ----------------------------------------------------------------------------
;; Wait for keypress. Handles repeat events.
;; ----------------------------------------------------------------------------
    .globl _wait_key
