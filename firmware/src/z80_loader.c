/*
 * Module z80_loader:
 *
 * Accepts a stream of bytes, unpacks it as a .z80 snapshot,
 * loads it into RAM, and executes it.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-  Patrik Persson
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stddef.h>

#include "z80_loader.h"

#include "context_switch.h"
#include "globals.h"
#include "menu.h"
#include "ui.h"

#pragma codeseg STAGE2

/* ------------------------------------------------------------------------- */

/* Size of a memory bank/page */
#define PAGE_SIZE                       (0x4000)

/* Special value for 'bytes_remaining' indicating no compression used */
#define BANK_LENGTH_UNCOMPRESSED        (0xffff)

/* Escape byte in compressed chunks */
#define Z80_ESCAPE                      (0xED)

/* Attribute address for large digits (kilobyte counter) */
#define ATTR_DIGIT_ROW  (0x5a00)

/* ------------------------------------------------------------------------- */

/* Pointer to received TFTP data */
static const uint8_t *received_data;

/* Number of valid bytes remaining in received_data */
static uint16_t received_data_length;

/*
 * Bytes remaining to unpack in current chunk
 */
static uint16_t chunk_bytes_remaining;

/* State for a repetition sequence */
/* set: chunk_compressed_repcount read: chunk_compressed_repetition */
static uint8_t rep_count;

/* Byte value for repetition */
static uint8_t rep_value;

/* ========================================================================= */

/*
 * The Z80 snapshot state machine is implemented by one function for each
 * state. The function returns whenever one of the following happens:
 *
 * - all currently available data has been consumed (received_data_length == 0)
 * - a state transition is required
 * - the write pointer has reached an integral number of kilobytes
 *   (the outer loop then manages evacuation)
 */

/* True if p is an integral number of kilobytes */
#define IS_KILOBYTE(p) \
  ((LOBYTE(p)) == 0 && ((HIBYTE(p) & 0x03) == 0))

/* Syntactic sugar for declaring and defining states */
#define DECLARE_STATE(s)                void s (void)
#define DEFINE_STATE(s)                 DECLARE_STATE(s) __naked

typedef void state_func_t(void);

state_func_t *z80_loader_state;

// digits (BCD) for progress display while loading a snapshot
static uint8_t digits;

/*
 * Offset to the R register when stored, to compensate for the fact that R
 * is affected by the execution of the trampoline.
 *
 * Calibrate this offset as follows:
 *
 * - Set it temporarily to 0, and run one of the test images
 * - Assume E = expected value of R        (0x2e in test image),
 *          N = actual value of R (as presented in binary by the test image)
 * - Set REG_R_ADJUSTMENT := (E - N)
 */
#define REG_R_ADJUSTMENT                    (0xEF)

/* ========================================================================= */

/*
 * State functions
 */
DECLARE_STATE(s_header);

DECLARE_STATE(s_chunk_uncompressed);  /* uncompressed data */
DECLARE_STATE(s_chunk_compressed);    /* compressed data */

DECLARE_STATE(s_chunk_header);        /* first byte of chunk 3-byte header */
DECLARE_STATE(s_chunk_header2);       /* second byte of chunk 3-byte header */
DECLARE_STATE(s_chunk_header3);       /* third byte of chunk 3-byte header */

DECLARE_STATE(s_chunk_compressed_escape); /* escape byte found */
DECLARE_STATE(s_chunk_single_escape);     /* single escape, no repetition */
DECLARE_STATE(s_chunk_repcount);          /* repetition length */
DECLARE_STATE(s_chunk_repvalue);          /* repetition value */
DECLARE_STATE(s_chunk_repetition);        /* write repeated value */

/* ------------------------------------------------------------------------- */

// uses AF, BC, HL
static void
update_progress_display(void)
__naked
{
  __asm

    ld    bc, #_digits
    ld    a, (bc)
    inc   a
    daa
    push  af             ;; remember flags
    ld    (bc), a
    jr    nz, not_100k   ;; turned from 99->100?

    ;; Number of kilobytes became zero in BCD:
    ;; means it just turned from 99 to 100.
    ;; Print the digit '1' for hundreds.

    ld    l, a   ;; L is now 0
    inc   a      ;; A is now 1
    call  _show_attr_digit
    ld    a, (bc)

not_100k::
    pop   hl             ;; recall flags, old F is now in L
    bit   #4, l          ;; was H flag set? Then the tens have increased
    jr    z, not_10k

    ;; Print tens (_x_)

    rra
    rra
    rra
    rra
    ld    l, #7
    call  _show_attr_digit

not_10k::
    ;; Print single-number digit (__x)

    ld    a, (bc)
    ld    l, #14
    call  _show_attr_digit

    ;; ************************************************************************
    ;; update progress bar
    ;; ************************************************************************

    ld    a, (_kilobytes_expected)
    sub   a, #48     ;; 48k snapshot?
    ld    h, a       ;; if it is, store zero in H (useful later)
    ld    a, (_kilobytes_loaded)
    jr    z, 00003$
    srl   a          ;; 128k snapshot => progress = kilobytes / 4
    srl   a
    jr    00002$

00003$:   ;; 48k snapshot, multiply A by approximately 2/3
          ;; approximated here as (A-1)*11/16

    dec   a
    ld    l, a
    ld    b, h
    ld    c, a
    add   hl, hl
    add   hl, hl
    add   hl, hl
    add   hl, bc
    add   hl, bc
    add   hl, bc

    ;; instead of shifting HL 4 bits right, shift 4 bits left, use H
    add   hl, hl
    add   hl, hl
    add   hl, hl
    add   hl, hl
    ld    a, h

00002$:
    or    a
    ret   z
    ld    hl, #PROGRESS_BAR_BASE-1
    add   a, l
    ld    l, a
    ld    (hl), #(PAPER(WHITE) + INK(WHITE) + BRIGHT)
    ret

  __endasm;
}


/* ------------------------------------------------------------------------- */

/*
 * subroutine: show huge digit in attributes, on row ATTR_DIGIT_ROW and down
 * L: column (0..31)
 * A: digit (0..9), bits 4-7 are ignored
 *
 * Destroys DE, saves BC
 */
void
show_attr_digit(void)
__naked
{
  __asm

    push  bc
    ld    de, #_font_data + 8 * 16 + 1   ;; address of '0' bits
    and   a, #0xf
    add   a, a
    add   a, a
    add   a, a
    add   a, e
    ld    e, a

    ld    h, #>ATTR_DIGIT_ROW

show_attr_digit_address_known::   ;; special jump target for init_progress_display
    ;; NOTE: must have stacked BC+DE before jumping here

    ld    c, #6
00001$:
    ld    a, (de)
    add   a, a
    inc   de
    ld    b, #6
00002$:
    add   a, a
    jr    nc, 00003$
    ld    (hl), #PAPER(WHITE) + INK(WHITE)
    jr    00004$
00003$:
    ld    (hl), #PAPER(BLACK) + INK(BLACK)
00004$:
    inc   hl
    djnz  00002$

    ld    a, #(ROW_LENGTH-6)
    add   a, l
    ld    l, a

    dec   c
    jr    nz, 00001$

    pop   bc
    ret

  __endasm;
}

/*
 * Evacuate data from the temporary buffer to ENC28J60 SRAM. Examine the stored
 * .z80 header, and prepare the context switch to use information
 * (register state etc.) in it.
 */
static void
evacuate_data(void)
__naked
{
  __asm

    ;; ========================================================================
    ;; Clear out the top-left five character cells, by setting ink colour
    ;; to the same as the paper colour. Which colour is chosen depends on how
    ;; many pixels are set in that particular character cell.
    ;;
    ;; (These character cells are used as temporary storage for the trampoline
    ;; below.)
    ;; ========================================================================

    ld   bc, #EVACUATION_TEMP_BUFFER
    ld   hl, #BITMAP_BASE

    ld   d, #5
evacuate_data_loop1::  ;;  loop over character cells
      ld   e, #0       ;;  accumulated bit weight
      push bc

      ld   c, #8
evacuate_data_loop2::  ;;  loop over pixel rows in cell
        ld   a, (hl)
        inc  h

        ld   b, #8
evacuate_data_loop3::  ;;  loop over pixels in cell
          rra
          jr   nc, pixel_not_set
          inc  e
pixel_not_set::
        djnz evacuate_data_loop3

        dec  c
      jr   nz, evacuate_data_loop2

      ld   bc, #0x7ff  ;;  decrease for loop above + increase to next cell
      xor  a           ;;  clear C flag
      sbc  hl, bc
      pop  bc

      ld   a, e
      cp   #33         ;;  more than half of the total pixels in cell
      ld   a, (bc)
      jr   nc, evac_use_fg

      ;; few pixels set -- use background color

      rra
      rra
      rra

evac_use_fg::  ;; many pixels set -- use foreground color

      and  #7

      ld   e, a
      add  a, a
      add  a, a
      add  a, a
      or   a, e

evac_colour_set::
      ld   (bc), a
      inc  bc
      dec  d
    jr   nz, evacuate_data_loop1

    ;; ------------------------------------------------------------------------
    ;; write JP nn instructions to VRAM trampoline, at positions 0x40X2
    ;; ------------------------------------------------------------------------

    ld   h, #0x40
    ld   b, #6
write_trampoline_loop::
      ld   l, #2
      ld   (hl), #0xc3        ;; JP nn
      inc  hl
      ld   (hl), #0           ;; low byte of JP target is 0
      inc  hl
      ld   (hl), h
      inc  (hl)               ;; high byte of JP target
      inc  h
    djnz   write_trampoline_loop

    ;; ------------------------------------------------------------------------
    ;; write OUT(SPI_OUT), A to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #0xD3 + 0x100 * SPI_OUT    ;; *0x4000 = OUT(SPI_OUT), A
    ld   (VRAM_TRAMPOLINE_OUT), hl

    ;; ------------------------------------------------------------------------
    ;; write LD A, x to trampoline  (value to be stored in I)
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_I - 1)
    ld   l, #0x3E                  ;; LD A, n
    ld   (VRAM_TRAMPOLINE_LD_A_FOR_I), hl

    ;; ------------------------------------------------------------------------
    ;; write LD A, x to trampoline  (actual value for A)
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_A)
    ld   h, a
    ld   (VRAM_TRAMPOLINE_LD_A), hl

    ;; ------------------------------------------------------------------------
    ;; write LD I, A to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #0x47ED
    ld   (VRAM_TRAMPOLINE_LD_I), hl

    ;; ------------------------------------------------------------------------
    ;; write NOP and IM0/IM1/IM2 to trampoline
    ;; ------------------------------------------------------------------------

    ld   hl, #VRAM_TRAMPOLINE_NOP
    ld   (hl), l                            ;; *0x4500 = NOP
    dec  h
    ld   (hl), #0xED                        ;; *0x04400 = first byte of IMx
    inc  l
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_INT_MODE)
    ld   b, #0x46                           ;; second byte of IM0
    and  a, #3
    jr   z, im_set
    ld   b, #0x56                           ;; second byte of IM1
    dec  a
    jr   z, im_set
    ld   b, #0x5E                           ;; second byte of IM2
im_set::
    ld   (hl), b

    ;; ------------------------------------------------------------------------
    ;; write EI or NOP to trampoline, depending on IFF1 state in snapshot
    ;; ------------------------------------------------------------------------

    inc  h                                  ;; now back at 0x4501
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_IFF1)
    or   a, a
    jr   z, evacuate_di     ;; flag byte is zero, which also happens to be NOP
    ld   a, #0xFB           ;; EI
evacuate_di::
    ld   (hl), a

    ;; ------------------------------------------------------------------------
    ;; write register state to VRAM trampoline area
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_R)
    add  a, #REG_R_ADJUSTMENT
    and  a, #0x7f
    ld   b, a
    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #0x01
    rrca
    or   a, b
    ld   (VRAM_REGSTATE_R), a

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_F
    ld   de, #VRAM_REGSTATE_F
    ld   bc, #5                  ;; F + BC + HL
    ldi

    ;; HL now points to _snapshot_header + Z80_HEADER_OFFSET_BC_HL
    ld   de, #VRAM_REGSTATE_BC_HL_F
    ldir

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_SP)
    ld   (VRAM_REGSTATE_SP), hl

    ;; ========================================================================
    ;; set PC value in VRAM trampoline, and clean up the values of
    ;; these fields in header:
    ;;   MISC_FLAGS,    to a good border value (0..7)
    ;;   A_P, F_P,      switched to make a single POP possible
    ;;   HW_TYPE,       to be either 0 (48k) or non-zero (128k)
    ;;   HW_STATE_7FFD, to a good default value also for 48k snapshots
    ;; ========================================================================

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l      ;; extended snapshot (version 2+) ?
    jr   nz, evacuate_pc_z80v1_or_48k

    ;; ------------------------------------------------------------------------
    ;; snapshot version 2+: use PC value from extended snapshot header,
    ;; load HW_TYPE into C, and memory config into B
    ;; ------------------------------------------------------------------------

    ld   hl, (_snapshot_header + Z80_HEADER_OFFSET_EXT_PC)
    ld   bc, (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)

    ;; ------------------------------------------------------------------------
    ;; The HW_TYPE field interpretation depends on the .z80 snaphost
    ;; version (sigh). So if the length field is >= 24 (i.e., version 3),
    ;; HW_TYPE needs to be decreased by one.
    ;; ------------------------------------------------------------------------

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_EXT_LENGTH)   ;; low byte
    cp   a, #24
    jr   c, evacuate_pc_z80v2      ;; C set if version 2 snapshot: keep HW_TYPE
    dec  c                              ;; version 3 snapshot: decrease HW_TYPE
evacuate_pc_z80v2::
    ld   a, c
    cp   a, #HW_TYPE_SPECTRUM_128K
    jr   nc, evacuate_pc                 ;; 128k snapshot: keep config as it is
evacuate_pc_z80v1_or_48k::
    ld   b, #MEMCFG_ROM_LO + MEMCFG_LOCK + DEFAULT_BANK
    xor  a
evacuate_pc::
    ld   (VRAM_REGSTATE_PC), hl
    ld   c, a
    ld   (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE), bc

    ;; ------------------------------------------------------------------------
    ;; clean up MISC_FLAGS, turn it into a value ready for OUT (0xFE), A
    ;; ------------------------------------------------------------------------

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_MISC_FLAGS
    ld   a, (hl)
    rra
    and    a, #0x07
    ld   (hl), a

    ;; ------------------------------------------------------------------------
    ;; swap A_P and F_P (to make simple POP in context switch possible)
    ;; ------------------------------------------------------------------------

    ld   hl, #_snapshot_header + Z80_HEADER_OFFSET_A_P
    ld   a, (hl)
    inc  hl
    ld   b, (hl)
    ld   (hl), a
    dec  hl
    ld   (hl), b

    ;; ========================================================================
    ;; write evacuated data to ENC28J60 RAM
    ;; ========================================================================

    ld   hl, #ENC28J60_EVACUATED_DATA
    ld   a, #ENC_OPCODE_WCR(EWRPTL)
    call _enc28j60_write_register16

    ld   de, #RUNTIME_DATA_LENGTH
    ld   hl, #EVACUATION_TEMP_BUFFER

    jp   _enc28j60_write_memory_cont

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * If the number of bytes loaded reached an even kilobyte,
 * increase kilobyte counter and update status display
 */
static void
update_progress(void)
__naked
{
  __asm

    ld   hl, (_tftp_write_pos)

    ;; check if HL is an integral number of kilobytes,
    ;; return early otherwise

    xor  a
    or   l
    ret  nz
    ld   a, h
    and  #0x03
    ret  nz

    ;; update the status display

    ld    hl, #_kilobytes_loaded
    inc   (hl)
    push  hl
    call  _update_progress_display
    pop   hl

    ;; if all data has been loaded, perform the context switch

    ld    a, (_kilobytes_expected)
    cp    a, (hl)
    ret   nz

#ifdef PAINT_STACK
    di
    halt
#else
    jp    _context_switch
#endif

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Returns *received_data++ in A
 * also decreases received_data_length
 *
 * (reads byte from received_data, increases received_data, returns byte in A)
 * Modifies HL (but not F)
 */
static void
get_next_byte(void)
__naked
{
  __asm

    ld   hl, (_received_data)
    ld   a, (hl)
    inc  hl
    ld   (_received_data), hl

    ld   hl, (_received_data_length)
    dec  hl
    ld   (_received_data_length), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Decreases chunk_bytes_remaining (byte counter in compressed chunk)
 */
static void
dec_chunk_bytes(void)
__naked
{
  __asm

    ld   hl, (_chunk_bytes_remaining)
    dec  hl
    ld   (_chunk_bytes_remaining), hl
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * State HEADER (initial):
 *
 * Evacuates the header from the TFTP data block. Returns the length of the
 * header (i.e., the offset of snapshot data within the TFTP data block)
 *
 * This function does some header parsing; it initializes compression_method
 * and verifies compatibility.
 */
DEFINE_STATE(s_header)
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; set bank 0 for 128k memory config
    ;; ------------------------------------------------------------------------

    xor  a, a
    ld   bc, #MEMCFG_ADDR
    out  (c), a

    ;; ------------------------------------------------------------------------
    ;; check snapshot header
    ;; ------------------------------------------------------------------------

    ;; set DE to .z80 snapshot header size
    ;; (initially the snapshot v1 size, modified later below)

    ld   de, #Z80_HEADER_OFFSET_EXT_LENGTH

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l
    jr   z, s_header_ext_hdr               ;; extended header?

    ;; ------------------------------------------------------------------------
    ;; not an extended header: expect a single 48k chunk
    ;; ------------------------------------------------------------------------

    ld   a, #>0xc000
    ld   (_chunk_bytes_remaining + 1), a       ;; low byte of is already zero

    ;; ------------------------------------------------------------------------
    ;; decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   hl, #_s_chunk_uncompressed
    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK
    jr   z, s_header_set_state
    ld   hl, #_s_chunk_compressed
    jr   s_header_set_state

s_header_ext_hdr::

    ;; ------------------------------------------------------------------------
    ;; extended header: adjust expected no. of kilobytes for a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_HW_TYPE)
    cp    a, #HW_TYPE_SPECTRUM_128K
    jr    c, s_header_not_128k
    ld    a, #128
    ld    (_kilobytes_expected), a

s_header_not_128k::

    ;; ------------------------------------------------------------------------
    ;; adjust header length
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   hl, de
    inc   hl
    inc   hl
    ex    de, hl

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    ld   hl, #_s_chunk_header

s_header_set_state::
    ld   (_z80_loader_state), hl

    ;; ------------------------------------------------------------------------
    ;; adjust _received_data and _received_data_length for header size
    ;; ------------------------------------------------------------------------

    ld   hl, (_received_data)
    add  hl, de
    ld   (_received_data), hl

    ld   hl, (_received_data_length)
    or   a, a            ;; clear C flag
    sbc  hl, de
    ld   (_received_data_length), hl

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header through loading and context switch
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #_snapshot_header
    ld   bc, #Z80_HEADER_RESIDENT_SIZE
    ldir

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/* receive low byte of chunk length */
DEFINE_STATE(s_chunk_header)
__naked
{
  __asm

    call _get_next_byte
    ld   (_chunk_bytes_remaining), a

    ld    hl, #_s_chunk_header2
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/* Receive high byte of chunk length */
DEFINE_STATE(s_chunk_header2)
__naked
{
  __asm

    call _get_next_byte
    ld   (_chunk_bytes_remaining + 1), a

    ld    hl, #_s_chunk_header3
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Receive ID of the page the chunk belongs to, range is 3..10
 *
 * See:
 * https://www.worldofspectrum.org/faq/reference/z80format.htm
 * https://www.worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
 */
DEFINE_STATE(s_chunk_header3)
__naked
{
  __asm

    ld   a, (_kilobytes_expected)
    ld   c, a

    call _get_next_byte

    cp   a, #3
    jr   c, s_chunk_header3_incompatible
    cp   a, #11
    jr   c, s_chunk_header3_compatible
s_chunk_header3_incompatible::
    ld   a, #FATAL_INCOMPATIBLE
    out  (0xfe), a
    di
    halt
s_chunk_header3_compatible::

    ;; Decide on a good value for tftp_write_pos; store in HL.

    ld   b, a    ;; useful extra copy of A

    ;;
    ;; Need to handle page 5 separately -- if we do not use the address range
    ;; 0x4000..0x7fff, the evacuation stuff in z80_parser will not work.
    ;;

    ld   h, #0x40
    cp   a, #8
    jr   z, s_chunk_header3_set_page

    ;;
    ;; Page 1 in a 48k snapshot points to 0x8000, but 128k snapshots are
    ;; different.
    ;;

    ld   h, #0x80
    cp   a, #4
    jr   nz, s_chunk_header3_default_page

    ld   a, #128
    cp   a, c    ;; 128k snapshot?
    jr   nz, s_chunk_header3_set_page

s_chunk_header3_default_page::

    ld   h, #0xc0
    ld   a, #128
    cp   a, c
    jr   nz, s_chunk_header3_set_page

    ;; If this is a 128k snapshot, switch memory bank

    ld   a, b
    sub  a, #3
    ld   bc, #MEMCFG_ADDR
    out  (c), a

s_chunk_header3_set_page::
    ld   l, #0
    ld   (_tftp_write_pos), hl

    ;; If chunk_bytes_remaining is 0xffff, length is 0x4000

    ld   hl, (_chunk_bytes_remaining)
    inc  h
    jr   nz, s_chunk_header3_compressed
    inc  l
    jr   nz, s_chunk_header3_compressed

    ld   h, #0x40    ;; HL is now 0x4000
    ld   (_chunk_bytes_remaining), hl

    ld    hl, #_s_chunk_uncompressed
    ld    (_z80_loader_state), hl

    ret

s_chunk_header3_compressed::

    ld    hl, #_s_chunk_compressed
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_uncompressed)
__naked
{
  __asm

  ;;
  ;; compute BC as minimum of
  ;; - distance to next kilobyte for tftp_write_pos
  ;; - received_data_length
  ;; - chunk_bytes_remaining
  ;;

  ld  hl, #_tftp_write_pos + 1
  ld  a, (hl)
  add #4            ;; round up to next 512-bytes boundary
  and #0xfc         ;; clears C flag, so sbc below works fine
  ld  h, a
  xor a
  ld  l, a
  ld  bc, (_tftp_write_pos)
  sbc hl, bc
  ld  b, h
  ld  c, l

  ;;
  ;; is received_data_length less than BC?
  ;; if it is, set BC to received_data_length
  ;;

  ld  hl, (_received_data_length)
  and a     ;; clear C flag for sbc below
  sbc hl, bc
  jr  nc, checked_length

  ld  bc, (_received_data_length)

checked_length::

  ;;
  ;; is chunk_bytes_remaining less than BC?
  ;; if it is, set BC to chunk_bytes_remaining
  ;;

  ld  hl, (_chunk_bytes_remaining)
  and a     ;; clear C flag for sbc below
  sbc hl, bc
  jr  nc, checked_chunk_length

  ld  bc, (_chunk_bytes_remaining)

checked_chunk_length::

  ;;
  ;; subtract BC from received_data_length and chunk_bytes_remaining
  ;;

  and a     ;; clear C flag for sbc below
  ld  hl, (_received_data_length)
  sbc hl, bc
  ld  (_received_data_length), hl

  ;;
  ;; subtract BC from chunk_bytes_remaining: if zero remains, set the next
  ;; state to s_chunk_header
  ;;

  ld  hl, (_chunk_bytes_remaining)
  sbc hl, bc
  ld  a, h
  or  l
  jr  nz, no_new_state

  ld  de, #_s_chunk_header
  ld  (_z80_loader_state), de

no_new_state::
  ld  (_chunk_bytes_remaining), hl

  ;;
  ;; if BC is zero, skip copying and status display update
  ;;
  ld  a, b
  or  c
  jr  z, no_copy

  ;;
  ;; Copy the required amount of data
  ;;

  ld  hl, (_received_data)
  ld  de, (_tftp_write_pos)
  ldir
  ld  (_received_data), hl
  ld  (_tftp_write_pos), de

  ;;
  ;; update the status display, if needed
  ;;

  call  _update_progress

no_copy::

  ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_compressed)
__naked
{
  __asm

  ld  bc, (_chunk_bytes_remaining)
  ld  de, (_received_data_length)
  ld  hl, (_tftp_write_pos)
  ld  iy, (_received_data)

s_chunk_compressed_loop::

  ;;
  ;; if chunk_bytes_remaining is zero, terminate loop and switch state
  ;;

  ld  a, b
  or  c
  jr  z, s_chunk_compressed_chunk_end

  ;;
  ;; if received_data_length is zero, terminate loop
  ;;

  ld  a, d
  or  e
  jp  z, s_chunk_compressed_write_back

  ;;
  ;; read a byte of input, increase read pointer,
  ;; decrease chunk_bytes_remaining and received_data_length
  ;;

  ld  a, (iy)
  inc iy
  dec bc
  dec de

  ;;
  ;; act on read data
  ;;

  cp  #Z80_ESCAPE
  jr  z, s_chunk_compressed_found_escape
  ld  (hl), a
  inc hl

  ;;
  ;; if HL is an integral number of kilobytes,
  ;; update the status display
  ;;

  xor a
  or  l
  jr  nz, s_chunk_compressed_loop
  ld  a, h
  and #0x03
  jr  nz, s_chunk_compressed_loop

  ld  (_chunk_bytes_remaining), bc
  ld  (_received_data_length), de
  ld  (_tftp_write_pos), hl
  ld  (_received_data), iy

  call _update_progress
  jr  s_chunk_compressed_done

  ;;
  ;; reached end of chunk: switch state
  ;;

s_chunk_compressed_chunk_end::
  ld  a, #<_s_chunk_header
  ld  (_z80_loader_state), a
  ld  a, #>_s_chunk_header
  ld  (_z80_loader_state+1), a
  jr  s_chunk_compressed_write_back

  ;;
  ;; found escape byte: switch state
  ;;

s_chunk_compressed_found_escape::
  ;;
  ;; optimization: if 3 bytes (or more) are available, and this is really
  ;; a repetition sequence, jump directly to s_chunk_repetition
  ;;

  ;; if bc < 3, goto s_chunk_compressed_no_opt
  ld  a, b
  or  a
  jr  nz, s_chunk_compressed_rept1
  ld  a, c
  cp  #3
  jr  c, s_chunk_compressed_no_opt

s_chunk_compressed_rept1::
  ;; if de < 3, goto s_chunk_compressed_no_opt
  ld  a, d
  or  a
  jr  nz, s_chunk_compressed_rept2
  ld  a, e
  cp  #3
  jr  c, s_chunk_compressed_no_opt

s_chunk_compressed_rept2::
  ld  a, (iy)       ;; peek, do not change counts
  cp  #Z80_ESCAPE
  jr  nz, s_chunk_compressed_no_opt

  ;;
  ;; the optimization is possible -- read the sequence data and jump
  ;; to the correct state
  ;;
  inc iy
  ld  a, (iy)
  inc iy
  ld  (_rep_count), a
  ld  a, (iy)
  inc iy
  ld  (_rep_value), a

  dec bc
  dec bc
  dec bc
  dec de
  dec de
  dec de

  ld  (_chunk_bytes_remaining), bc
  ld  (_received_data_length), de
  ld  (_tftp_write_pos), hl
  ld  (_received_data), iy

  ld  hl, #_s_chunk_repetition
  ld  (_z80_loader_state), hl
jp_hl_instr::          ;; convenient CALL target
  jp  (hl)

s_chunk_compressed_no_opt::
  ;;
  ;; no direct jump to s_chunk_repetition was possible
  ;;

  ld  a, #<_s_chunk_compressed_escape
  ld  (_z80_loader_state), a
  ld  a, #>_s_chunk_compressed_escape
  ld  (_z80_loader_state+1), a

s_chunk_compressed_write_back::
  ld  (_chunk_bytes_remaining), bc
  ld  (_received_data_length), de
  ld  (_tftp_write_pos), hl
  ld  (_received_data), iy

s_chunk_compressed_done::

  ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_compressed_escape)
__naked
{
  __asm

    call  _get_next_byte
    call  _dec_chunk_bytes

    cp    a, #Z80_ESCAPE
    jr    nz, 00001$

    ld    hl, #_s_chunk_repcount
    ld    (_z80_loader_state), hl

    ret

00001$:
    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not a compressed sequence
    ;;

    push  af

    ld    hl, (_tftp_write_pos)
    ld    (hl), #Z80_ESCAPE
    inc   hl
    ld    (_tftp_write_pos), hl
    call  _update_progress

    pop   af

    ld    hl, (_tftp_write_pos)
    ld    (hl), a
    inc   hl
    ld    (_tftp_write_pos), hl
    call  _update_progress

    ld    hl, #_s_chunk_compressed
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repcount)
__naked
{
  __asm

    call _get_next_byte
    ld   (_rep_count), a

    call _dec_chunk_bytes

    ld    hl, #_s_chunk_repvalue
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repvalue)
__naked
{
  __asm

    call _get_next_byte
    ld   (_rep_value), a

    call _dec_chunk_bytes

    ld    hl, #_s_chunk_repetition
    ld    (_z80_loader_state), hl

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repetition)
__naked
{
  __asm

  ld  a, (_rep_count)
  ld  b, a                      ;; loop counter rep_count
  ld  hl, (_tftp_write_pos)
  ld  a, (_rep_value)
  ld  c, a

s_chunk_repetition_loop::
  ld  a, b
  or  a
  jr  z, s_chunk_repetition_write_back

  ld  (hl), c
  inc hl
  dec b

  ;;
  ;; if HL is an integral number of kilobytes,
  ;; update the status display
  ;;

  ld  a, l
  or  a
  jr  nz, s_chunk_repetition_loop
  ld  a, h
  and #0x03
  jr  nz, s_chunk_repetition_loop

  ld  a, b
  ld  (_rep_count), a
  ld  (_tftp_write_pos), hl

  jp  _update_progress

s_chunk_repetition_write_back::
  ld  (_rep_count), a           ;; copied from b above
  ld  (_tftp_write_pos), hl

  ld    hl, #_s_chunk_compressed
  ld    (_z80_loader_state), hl

  ret

__endasm;
}

/* ------------------------------------------------------------------------- */

/* Indicates an evacuation is ongoing (see below), initially false */
static bool evacuating;

void
z80_loader_receive_hook(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; set up _received_data & _received_data_length
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   (_received_data), hl

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH)
    ld   a, l      ;; byteswap: length is stored in network order in UDP header
    ld   l, h
    ld   h, a
    ld   bc, #0x10000 - UDP_HEADER_SIZE - TFTP_HEADER_SIZE
    add  hl, bc
    ld   (_received_data_length), hl

    ;; ========================================================================
    ;; read bytes, evacuate when needed, call state functions
    ;; ========================================================================

receive_snapshot_byte_loop::

    ;; ------------------------------------------------------------------------
    ;; if received_data_length is zero, we are done
    ;; ------------------------------------------------------------------------

    ld    hl, (_received_data_length)
    ld    a, h
    or    a, l
    ret   z

    ;; ------------------------------------------------------------------------
    ;; check evacuation status only if low byte of _tftp_write_pos is zero
    ;; ------------------------------------------------------------------------

    ld    hl, #_tftp_write_pos
    ld    a, (hl)
    or    a, a
    jr    nz, receive_snapshot_no_evacuation

    ;; ------------------------------------------------------------------------
    ;; reached RUNTIME_DATA (resident area)?
    ;; ------------------------------------------------------------------------

    ld    de, #_evacuating

    inc   hl
    ld    a, (hl)
    cp    a, #>RUNTIME_DATA
    jr    nz, receive_snapshot_not_entering_runtime_data

    ;; ------------------------------------------------------------------------
    ;; then store data in EVACUATION_TEMP_BUFFER instead,
    ;; and set "evacuating" flag
    ;; ------------------------------------------------------------------------

    ld    a, #>EVACUATION_TEMP_BUFFER
    ld    (hl), a
    ld    (de), a      ;; != 0, so fine here as a flag value

    jr    receive_snapshot_no_evacuation

receive_snapshot_not_entering_runtime_data::

    ;; ------------------------------------------------------------------------
    ;; is an evacuation about to be completed?
    ;; ------------------------------------------------------------------------

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
    jr    nz, receive_snapshot_no_evacuation

    ld    a, (de)
    or    a, a
    jr    z, receive_snapshot_no_evacuation

    ;; ------------------------------------------------------------------------
    ;; then set _tftp_write_pos := RUNTIME_DATA + RUNTIME_DATA_LENGTH,
    ;; and _evacuating := false
    ;; ------------------------------------------------------------------------

    ld    a, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH)
    ld    (hl), a

    xor   a, a
    ld    (de), a

    ;; ------------------------------------------------------------------------
    ;; copy the evacuated data to ENC28J60 RAM,
    ;; and make some preparations for context switch
    ;; ------------------------------------------------------------------------

    call  _evacuate_data

receive_snapshot_no_evacuation::

    ;; ------------------------------------------------------------------------
    ;; call function pointed to by _z80_loader_state
    ;; there is no "CALL (HL)" instruction, so CALL a JP (HL) instead
    ;; ------------------------------------------------------------------------

    ld    hl, (_z80_loader_state)
    call  jp_hl_instr

    jr    receive_snapshot_byte_loop

  __endasm;
}
