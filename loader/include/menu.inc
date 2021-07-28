;;
;; Module menu:
;;
;; Display a menu from the loaded snapshot file, and load selected snapshot.
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
;; Location of local and server IP addresses (row 23, columns 0 and 16)
;; ----------------------------------------------------------------------------

LOCAL_IP_POS   = (BITMAP_BASE + 0x1100 + 7*32 + 0)
SERVER_IP_POS  = (BITMAP_BASE + 0x1100 + 7*32 + 16)

LOCAL_IP_ATTR  = (ATTRS_BASE + 23*32 + 0)
SERVER_IP_ATTR = (ATTRS_BASE + 23*32 + 16)

;; ----------------------------------------------------------------------------
;; First state for TFTP loader: load a menu file and display it.
;; ----------------------------------------------------------------------------

    .globl tftp_state_menu_loader