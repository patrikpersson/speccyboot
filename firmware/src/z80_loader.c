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

/* ------------------------------------------------------------------------- */

/* Size of a memory bank/page */
#define PAGE_SIZE                       (0x4000)

/* Special value for 'bytes_remaining' indicating no compression used */
#define BANK_LENGTH_UNCOMPRESSED        (0xffff)

/* Escape byte in compressed chunks */
#define Z80_ESCAPE                      (0xED)

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
#define DECLARE_STATE(s)                static void s (void)
#define DEFINE_STATE(s)                 DECLARE_STATE(s)

typedef void state_func_t(void);

static state_func_t *current_state = &s_header;

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

/*
 * If the number of bytes loaded reached an even kilobyte,
 * increase kilobyte counter and update status display
 */
static void
update_progress(void)
__naked
{
  __asm

    ld   hl, (_curr_write_pos)

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
  uint16_t header_length = offsetof(struct z80_snapshot_header_t,
                                    extended_length);

  memcfg(DEFAULT_BANK);

  if (IS_EXTENDED_SNAPSHOT_HEADER(&rx_frame.udp.app.tftp.data.z80)) {
    if (IS_128K_MACHINE(rx_frame.udp.app.tftp.data.z80.hw_type)) {
      kilobytes_expected = 128;
    }
    header_length += rx_frame.udp.app.tftp.data.z80.extended_length + 2;

    current_state = &s_chunk_header;
  } else {
    chunk_bytes_remaining = 0xc000;

    current_state = (rx_frame.udp.app.tftp.data.z80.snapshot_flags
                     & SNAPSHOT_FLAGS_COMPRESSED_MASK)
                  ? &s_chunk_compressed : &s_chunk_uncompressed;
  }

  evacuate_z80_header();

  received_data        += header_length;
  received_data_length -= header_length;
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
    ld    (_current_state), hl

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
    ld    (_current_state), hl

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

    ;; Decide on a good value for curr_write_pos; store in HL.

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
    ld   (_curr_write_pos), hl

    ;; If chunk_bytes_remaining is 0xffff, length is 0x4000

    ld   hl, (_chunk_bytes_remaining)
    inc  h
    jr   nz, s_chunk_header3_compressed
    inc  l
    jr   nz, s_chunk_header3_compressed

    ld   h, #0x40    ;; HL is now 0x4000
    ld   (_chunk_bytes_remaining), hl

    ld    hl, #_s_chunk_uncompressed
    ld    (_current_state), hl

    ret

s_chunk_header3_compressed::

    ld    hl, #_s_chunk_compressed
    ld    (_current_state), hl

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
  ;; - distance to next kilobyte for curr_write_pos
  ;; - received_data_length
  ;; - chunk_bytes_remaining
  ;;

  ld  hl, #_curr_write_pos + 1
  ld  a, (hl)
  add #4            ;; round up to next 512-bytes boundary
  and #0xfc         ;; clears C flag, so sbc below works fine
  ld  h, a
  xor a
  ld  l, a
  ld  bc, (_curr_write_pos)
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
  ld  (_current_state), de

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
  ld  de, (_curr_write_pos)
  ldir
  ld  (_received_data), hl
  ld  (_curr_write_pos), de

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
  ld  hl, (_curr_write_pos)
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
  ld  (_curr_write_pos), hl
  ld  (_received_data), iy

  call _update_progress
  jr  s_chunk_compressed_done

  ;;
  ;; reached end of chunk: switch state
  ;;

s_chunk_compressed_chunk_end::
  ld  a, #<_s_chunk_header
  ld  (_current_state), a
  ld  a, #>_s_chunk_header
  ld  (_current_state+1), a
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
  ld  (_curr_write_pos), hl
  ld  (_received_data), iy

  ld  hl, #_s_chunk_repetition
  ld  (_current_state), hl
  jp  (hl)

s_chunk_compressed_no_opt::
  ;;
  ;; no direct jump to s_chunk_repetition was possible
  ;;

  ld  a, #<_s_chunk_compressed_escape
  ld  (_current_state), a
  ld  a, #>_s_chunk_compressed_escape
  ld  (_current_state+1), a

s_chunk_compressed_write_back::
  ld  (_chunk_bytes_remaining), bc
  ld  (_received_data_length), de
  ld  (_curr_write_pos), hl
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
    ld    (_current_state), hl

    ret

00001$:
    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not a compressed sequence
    ;;

    push  af

    ld    hl, (_curr_write_pos)
    ld    (hl), #Z80_ESCAPE
    inc   hl
    ld    (_curr_write_pos), hl
    call  _update_progress

    pop   af

    ld    hl, (_curr_write_pos)
    ld    (hl), a
    inc   hl
    ld    (_curr_write_pos), hl
    call  _update_progress

    ld    hl, #_s_chunk_compressed
    ld    (_current_state), hl

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
    ld    (_current_state), hl

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
    ld    (_current_state), hl

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
  ld  hl, (_curr_write_pos)
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
  ld  (_curr_write_pos), hl

  jp  _update_progress

s_chunk_repetition_write_back::
  ld  (_rep_count), a           ;; copied from b above
  ld  (_curr_write_pos), hl

  ld    hl, #_s_chunk_compressed
  ld    (_current_state), hl

  ret

__endasm;
}

/* ------------------------------------------------------------------------- */

static void
receive_snapshot_data(void)
{
  /* Indicates an evacuation is ongoing (see below) */
  static bool evacuating = false;

  received_data        = TFTP_DATA_BUFFER;
  received_data_length = ntohs(rx_frame.udp.header.length)
                         - sizeof(struct udp_header_t)
                         - sizeof(struct tftp_header_t);

  while (received_data_length != 0) {
    if ((LOBYTE(curr_write_pos) == 0)) {
      if (HIBYTE(curr_write_pos) == HIBYTE(RUNTIME_DATA)) {
        curr_write_pos = (uint8_t *) EVACUATION_TEMP_BUFFER;
        evacuating     = true;
      }
      else if (evacuating
               && (HIBYTE(curr_write_pos) ==
                   HIBYTE(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)))
      {
        evacuate_data();
        curr_write_pos = (uint8_t *) RUNTIME_DATA + RUNTIME_DATA_LENGTH;
        evacuating     = false;
      }
    }

    (*current_state)();
  }
}

/* ========================================================================= */

void
expect_snapshot(void)
__naked
{
  __asm

    ld   hl, #0x4000
    ld   (_curr_write_pos), hl

    ld    hl, #_receive_snapshot_data
    ld    (_tftp_receive_hook), hl

    ret

  __endasm;
}
