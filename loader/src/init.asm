  ;; init:
  ;;
  ;; System initialization, RST & interrupt handlers
  ;;
  ;; Part of SpeccyBoot <https://patrikpersson.github.io/speccyboot/>
  ;; --------------------------------------------------------------------------
  ;;
  ;; Copyright (c) 2009-  Patrik Persson & Imrich Konkol
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
  ;; --------------------------------------------------------------------------

  .module init

  .include "enc28j60.inc"
  .include "eth.inc"
  .include "globals.inc"
  .include "menu.inc"
  .include "spi.inc"
  .include "tftp.inc"
  .include "udp_ip.inc"
  .include "util.inc"

  ;; --------------------------------------------------------------------------

  .area  _HEADER (ABS)

  ;; --------------------------------------------------------------------------

  .org  0

  im    1                    ;; 2 bytes
  ld    hl, #_stack_top      ;; 3 bytes
  ld    sp, hl               ;; 1 byte

  jr    init_continued       ;; 2 bytes

  ;; ========================================================================
  ;; RST 0x08 ENTRYPOINT: enc28j60_write_register16
  ;; ========================================================================

  .org   0x08

  ld     e, a
  inc    e
  ld     d, h

  ld     h, l
  ld     l, a

  rst   enc28j60_write8plus8

  ex     de, hl

  .db    JR_UNCONDITIONAL         ;; JR to 0x0018: enc28j60_write8plus8

  ;; ========================================================================
  ;; RST 0x10 ENTRYPOINT: enc28j60_select_bank
  ;; ========================================================================

  .org  0x10

  .db    7         ;; RLCA (has no effect), and offset for JR above   1 byte

  ;; ------------------------------------------------------------------------
  ;; clear bits 0 and 1 of register ECON1
  ;; ------------------------------------------------------------------------

  ld   hl, #0x0100 * 0x03 + OPCODE_BFC + (ECON1 & REG_MASK)        ;; 3 bytes
  rst  enc28j60_write8plus8                                        ;; 1 byte

  ;; ------------------------------------------------------------------------
  ;; mask in "bank" in bits 0 and 1 of register ECON1
  ;; ------------------------------------------------------------------------

  ld   l, #OPCODE_BFS + (ECON1 & REG_MASK)                         ;; 2 bytes
  ld   h, e                                                        ;; 1 byte

  ;; FALL THROUGH to enc28j60_write8plus8

  ;; ========================================================================
  ;; RST 0x18 ENTRYPOINT: enc28j60_write8plus8
  ;; ========================================================================

  .org  0x18

  ld    c, l                                                    ;; 1 byte
  rst   spi_write_byte                                          ;; 1 byte
  ld    c, h                                                    ;; 1 byte
  rst   spi_write_byte                                          ;; 1 byte
  jr    enc28j60_end_transaction_and_return                     ;; 2 bytes

  ;; two-byte slot remains


  ;; ========================================================================
  ;; RST 0x20 ENTRYPOINT: spi_write_byte
  ;; ========================================================================
  
  .org  0x20
  
  ld    b, #8                                            ;; 2 bytes
  jr    spi_write_byte_cont                              ;; 2 bytes

enc28j60_write_local_hwaddr:

  ld    e, #ETH_ADDRESS_SIZE                             ;; 2 bytes

  .db   LD_HL_NN, <eth_local_address                     ;; 2 bytes + 1 below

  ;; FALL THROUGH to enc28j60_write_memory_small

  ;; ========================================================================
  ;; RST 0x28 ENTRYPOINT: enc28j60_write_memory_small
  ;; ========================================================================

  .org  0x28
  
  .db   0     ;; one NOP byte, also high address byte of LD HL, #nn;  1 byte
  ld    d, #0                                                      ;; 2 bytes

  ;; ========================================================================
  ;; enc28j60_write_memory entrypoint
  ;; ========================================================================

enc28j60_write_memory:

  ;; ------------------------------------------------------------------------
  ;; start transaction: WBM
  ;; ------------------------------------------------------------------------

  ld    c, #OPCODE_WBM                                             ;; 2 bytes
  rst   spi_write_byte                                             ;; 1 byte

  jr    enc28j60_write_memory_cont                                 ;; 2 bytes

  ;; ========================================================================
  ;; RST 0x30 ENTRYPOINT: enc28j60_write_memory_inline
  ;; ========================================================================

  .org	0x30

  pop    hl
  ld     e, (hl)
  inc    hl
  rst    enc28j60_write_memory_small
  jp     (hl)

  ;; ========================================================================
  ;; BOOTP header defaults
  ;; ========================================================================

bootrequest_header_data:
  .db   1               ;; op, 1=BOOTREQUEST
  .db   1               ;; htype (10M Ethernet)
  .db   6               ;; hlen

  ;; NOTE: a fourth zero byte is represented by a NOP below

  ;; ========================================================================
  ;; RST 0x38 ENTRYPOINT: 50 Hz interrupt
  ;;
  ;; Increase 16-bit value at '_timer_tick_count' by 2
  ;; ========================================================================

  .org	0x38

  nop              ;; zero byte, HOP field from BOOTP header above

;; The BOOTP XID is arbitrary, and happens to be taken from the
;; interrupt handler code below.

bootrequest_xid:

  push  hl
  ld	  hl, (_timer_tick_count)
  inc	  hl
  inc	  hl
  ld	  (_timer_tick_count), hl
  pop   hl
  ei
  ret

;; ############################################################################
;; spi_write_byte_cont
;; ############################################################################

spi_write_byte_cont:
    SPI_WRITE_BIT_FROM  c
    djnz  spi_write_byte_cont

    ret

;; ############################################################################
;; enc28j60_write_memory_cont
;;
;; these bytes also double as Ethernet address:
;; 4E:23:E7:1B:7A:B3
;;
;; The lower two bits of the first byte are 01, so this is a locally
;; administered address:
;; https://en.wikipedia.org/wiki/MAC_address#Ranges_of_group_and_locally_administered_addresses
;; ############################################################################

enc28j60_write_memory_cont:
eth_local_address:

    ;; ------------------------------------------------------------------------
    ;; write DE bytes, starting at HL
    ;; ------------------------------------------------------------------------

    ld    c, (hl)  ;; read byte from data
    inc   hl

    rst   spi_write_byte   ;; preserves HL+DE, destroys AF+BC

    dec   de
    ld    a, d
    or    a, e
    jr    nz, enc28j60_write_memory_cont

    ;; ------------------------------------------------------------------------
    ;; end transaction
    ;; ------------------------------------------------------------------------

enc28j60_end_transaction_and_return:

    ld  a, #SPI_IDLE
    out (SPI_OUT), a
    ld  a, #SPI_IDLE+SPI_CS
    out (SPI_OUT), a

    ld  a, c

    ret

;; ===========================================================================
;; MAC address data (placed here to ensure high byte of address is zero)
;; ===========================================================================

eth_broadcast_address:
    .db   0xff, 0xff, 0xff, 0xff, 0xff, 0xff


  ;; ========================================================================
  ;; ARP Ethertype (0x08 0x06)
  ;; ========================================================================

ethertype_arp:

    .db   0x08

    ;; followed by 0x06 (LD B, #n) below


  ;; ==========================================================================
  ;; continued initialization (from 0x0000)
  ;; ==========================================================================

init_continued:

  ;; --------------------------------------------------------------------------
  ;; Perform a delay of about 200ms before accessing any memory, for the 128k
  ;; reset logic (bank switching) to initialize properly. Essentially the same
  ;; delay as initially performed in the standard 128k ROM 0.
  ;;
  ;; >= 0x6b00 iterations  x  26 T-states  >=  200.79ms @3.54690MHz
  ;; --------------------------------------------------------------------------

  ld    b, #0x6b      ;; BC := 0x6bxx ; the value of C doesn't matter much here
reset_delay:
  dec   bc
  ld    a, b
  or    a, c
  jr    nz, reset_delay

  ;; --------------------------------------------------------------------------
  ;; Configure memory banks, and ensure ROM1 (BASIC ROM) is paged in.
  ;; This sequence differs between SpeccyBoot and DGBoot, but preserves HL
  ;; in both cases. It is executed after the delay above, to ensure that
  ;; the 128k reset logic settles first.
  ;; --------------------------------------------------------------------------

  platform_init

  ;; --------------------------------------------------------------------------
  ;; Copy trampoline to RAM. Some extra iterations are added to ensure L ends
  ;; up being zero (useful later).
  ;;
  ;; NOTE: this is fragile.
  ;; --------------------------------------------------------------------------

  ex    de, hl   ;; DE now points to _stack_top
  ld    hl, #ram_trampoline
  push  de
  ld    bc, #0x0077    ;; slight overkill, tuned to ensure L ends up being zero
  ldir

  ret   ;; jump to _stack_top

  ;; --------------------------------------------------------------------------
  ;; Trampoline: copied to RAM above. Must be executed from RAM, since
  ;; it pages out the SpeccyBoot ROM.
  ;;
  ;; Copies font data to _font_data (defined in globals.inc). If CAPS SHIFT is
  ;; pressed, executes BASIC.
  ;; --------------------------------------------------------------------------

ram_trampoline:

  ld    a, #PAGE_OUT         ;; page out SpeccyBoot, keep ETH in reset
  out   (SPI_OUT), a

  ;; Is Caps Shift being pressed? Clear C flag if it is

  ld    a, #0xFE             ;; 0xFEFE: keyboard scan row CAPS..V
  in    a, (0xFE)
  rra

  jr    nc, go_to_basic      ;; if Caps Shift was pressed, go to BASIC

  ld    h, #0x3d             ;; 0x3d00 == ROM1 font data; L == 0 here
  ld    de, #_font_data      ;; address of font buffer in RAM
  ld    bc, #0x0300
  ldir

  xor   a                    ;; page in SpeccyBoot ROM, keep ETH in reset
  out   (SPI_OUT), a

  jp    initialize_global_data

go_to_basic:

  ;; --------------------------------------------------------------------------
  ;; Select ROM 0 (boot ROM), restart from there.
  ;; Ensure the right ROM is selected on a 128k/+2/+3 machine.
  ;; --------------------------------------------------------------------------

  ld    bc, #MEMCFG_ADDR
  out   (c), l               ;; L == 0 here
  ld    b, #>MEMCFG_PLUS_ADDR
  out   (c), l
  rst   #0

  ;; --------------------------------------------------------------------------
  ;; initialization of (mostly just clearing) global data
  ;; --------------------------------------------------------------------------

initialize_global_data:

  ;; clear bitmap VRAM (also used as a source of zeros for BOOTP)

  ld    de, #0x4001        ;; HL is 0x4000 here (after previous LDIR)
  ld    b, #0x18           ;; BC is now 0x1800 (BC was 0 after previous LDIR)
  ld    (hl), l
  ldir

  ;; set attribute VRAM: lines 0..22, PAPER+INK WHITE

  ld    (hl), #WHITE + (WHITE << 3)         ;; 0x3f
  ld    bc, #0x20 * 23
  ldir

  ;; set attribute VRAM: line 23, PAPER WHITE, INK BLACK
  ;; also paint stack

  ld    c, (hl)       ;; 0x3f: bottom attribute row + stack, minus one
  ld    (hl), #BLACK + (WHITE << 3)
  ldir

  ld    (hl), c       ;; C == 0 after LDIR above
  ld    bc, #_font_data - _stack_top + 1  ;; plus one, due to using 0x3f above
  ldir

  ld    h, #>stage2_start         ;; L == <font_data == 0

  ld    (_tftp_write_pos), hl

  ei

;; ===========================================================================

  ;; --------------------------------------------------------------------------
  ;; Ordering of segments for the linker
  ;; --------------------------------------------------------------------------

  .area _CODE

  .area _Z80_LOADER_STATES ;; states for .z80 loader (need to be in a single page)

  .area _DATA

  .area _STAGE2_ENTRY      ;; header for the stage 2 bootloader (RAM)

stage2_start:

  ;; --------------------------------------------------------------------------
  ;; Special mark for integrity check, as first byte in loaded binary:
  ;; VERSION_MAGIC
  ;; --------------------------------------------------------------------------

  .db   VERSION_MAGIC

  ;; --------------------------------------------------------------------------
  ;; If the version mark above checks out, execution continues here
  ;; --------------------------------------------------------------------------

  .area _NONRESIDENT          ;; continues here
  .area _SNAPSHOTLIST         ;; area for loaded snapshot list

nbr_snapshots:
  .ds    1

snapshot_array:
