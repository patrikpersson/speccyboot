/*
 * Module file_loader:
 *
 * Accepts a stream of bytes, and, depending on the current state, either:
 *
 * - unpacks it as a .z80 snapshot, loads it into RAM, and executes it.
 * - loads it as a raw file into a specified location.
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

#include "file_loader.h"

#include "context_switch.h"
#include "globals.h"
#include "menu.h"
#include "syslog.h"
#include "ui.h"

/* ------------------------------------------------------------------------- */

/* Size of a memory bank/page */
#define PAGE_SIZE                       (0x4000)

/* Special value for 'bytes_remaining' indicating no compression used */
#define BANK_LENGTH_UNCOMPRESSED        (0xffff)

/* Escape byte in compressed chunks */
#define Z80_ESCAPE                      (0xED)

/* ------------------------------------------------------------------------- */

/*
 * Bytes remaining to unpack in current chunk
 * (union to allow for writing of individual bytes)
 */
static union {
  uint16_t bytes;
  PACKED_STRUCT() {
    uint8_t bytes_lo;
    uint8_t bytes_hi;
  };
} chunk_state;

/* State for a repetition sequence */
static union {
  uint8_t plain_byte;   /* set: s_chunk_single_escape read: s_chunk_single_escape */
  uint8_t count;        /* set: chunk_compressed_repcount read: chunk_compressed_repetition */
} rep_state;

/* Byte value for repetition */
static uint8_t rep_value;

/* Pointer to received TFTP data */
static const uint8_t *received_data;

/* Number of valid bytes remaining in received_data */
static uint16_t received_data_length;

/*
 * For progress display.
 *
 * For 128k snapshots, 'kilobytes_expected' is updated in s_header below.
 */
static uint8_t kilobytes_loaded   = 0;
static uint8_t kilobytes_expected = 48;

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

#ifndef SB_MINIMAL
DECLARE_STATE(s_raw_data);                /* receive raw data file */
#endif

/* ------------------------------------------------------------------------- */

#ifdef SB_MINIMAL
static uint8_t *curr_write_pos = 0x4000;
static const state_func_t *current_state = &s_header;
#else
static uint8_t *curr_write_pos = (uint8_t *) snapshot_list_buf;
static const state_func_t *current_state = &s_raw_data;
#endif

/* ------------------------------------------------------------------------- */

/* Increase kilobyte counter, update status display */
static void
update_progress(void)
{
  kilobytes_loaded++;
#ifndef SB_MINIMAL
  static uint8_t digit_single = 0;
  static uint8_t digit_tens = 0;

  if (++digit_single > 9) {
    digit_single = 0;
    if (++digit_tens > 9) {
      digit_tens = 0;
      display_digit_at(1, 16, 12);
    }
    display_digit_at(digit_tens, 16, 19);
  }
  display_digit_at(digit_single, 16, 26);

  display_progress(kilobytes_loaded, kilobytes_expected);
#endif
  if (kilobytes_loaded == kilobytes_expected) {
    context_switch();
  }
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
    chunk_state.bytes = 0xc000;

    current_state = (rx_frame.udp.app.tftp.data.z80.snapshot_flags
                     & SNAPSHOT_FLAGS_COMPRESSED_MASK)
                  ? &s_chunk_compressed : &s_chunk_uncompressed;
  }

#ifndef SB_MINIMAL
  display_progress(0, kilobytes_expected);
#endif

  evacuate_z80_header();

  received_data        += header_length;
  received_data_length -= header_length;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header)
{
  /* Receive low byte of chunk length */
  chunk_state.bytes_lo = *received_data++;
  received_data_length--;

  current_state = &s_chunk_header2;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header2)
{
  /* Receive high byte of chunk length */
  chunk_state.bytes_hi = *received_data++;
  received_data_length--;

  current_state = &s_chunk_header3;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header3)
{
  /*
   * Receive ID of the page the chunk belongs to, range is 3..10
   *
   * See:
   * http://www.worldofspectrum.org/faq/reference/z80format.htm
   * http://www.worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
   */
  uint8_t page_id = *received_data++;
  received_data_length--;

  if (page_id < 3 || page_id > 10) {
    fatal_error(FATAL_INCOMPATIBLE);
  }

  switch (page_id) {
    case 8:
      /*
       * Need to handle page 5 separately -- if we don't use the address range
       * 0x4000..0x7fff, the evacuation stuff in z80_parser won't work.
       */
      curr_write_pos = (uint8_t *) 0x4000;
      break;
    case 4:
      /*
       * Page 1 in a 48k snapshot points to 0x8000, but 128k snapshots are
       * different.
       */
      if (kilobytes_expected != 128) {
        curr_write_pos = (uint8_t *) 0x8000;
        break;
      }
      /* else fall-through */
    default:
      if (kilobytes_expected == 128) {
        memcfg(page_id - 3);
      }
      curr_write_pos = (uint8_t *) 0xc000;
      break;
  }

  if (chunk_state.bytes == BANK_LENGTH_UNCOMPRESSED) {
    chunk_state.bytes = PAGE_SIZE;
    current_state = &s_chunk_uncompressed;
  }
  else {
    current_state = &s_chunk_compressed;
  }
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_uncompressed)
{
  __asm

  ;;
  ;; compute BC as minimum of
  ;; - distance to next kilobyte for curr_write_pos
  ;; - received_data_length
  ;; - chunk_state.bytes
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
  ;; is chunk_state.bytes less than BC?
  ;; if it is, set BC to chunk_state.bytes
  ;;

  ld  hl, (_chunk_state)
  and a     ;; clear C flag for sbc below
  sbc hl, bc
  jr  nc, checked_chunk_length

  ld  bc, (_chunk_state)

checked_chunk_length::

  ;;
  ;; subtract BC from received_data_length and chunk_state.bytes
  ;;

  and a     ;; clear C flag for sbc below
  ld  hl, (_received_data_length)
  sbc hl, bc
  ld  (_received_data_length), hl

  ;;
  ;; subtract BC from chunk_state.bytes: if zero remains, set the next
  ;; state to s_chunk_header
  ;;

  ld  hl, (_chunk_state)
  sbc hl, bc
  ld  a, h
  or  l
  jr  nz, no_new_state

  ld  de, #_s_chunk_header
  ld  (_current_state), de

no_new_state::
  ld  (_chunk_state), hl

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
  ;; if DE is an integral number of kilobytes,
  ;; update the status display
  ;;

  xor a
  or  e
  jr  nz, no_copy
  ld  a, d
  and #0x03
  jr  nz, no_copy

  call  _update_progress

no_copy::

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_compressed)
{
  __asm

  ld  bc, (_chunk_state)
  ld  de, (_received_data_length)
  ld  hl, (_curr_write_pos)
  ld  iy, (_received_data)

s_chunk_compressed_loop::

  ;;
  ;; if chunk_state.bytes is zero, terminate loop and switch state
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
  ;; decrease chunk_state.bytes and received_data_length
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

  ld  (_chunk_state), bc
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
  ld  (_rep_state), a
  ld  a, (iy)
  inc iy
  ld  (_rep_value), a

  dec bc
  dec bc
  dec bc
  dec de
  dec de
  dec de

  ld  (_chunk_state), bc
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
  ld  (_chunk_state), bc
  ld  (_received_data_length), de
  ld  (_curr_write_pos), hl
  ld  (_received_data), iy

s_chunk_compressed_done::

  __endasm;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_compressed_escape)
{
  uint8_t b = *received_data++;
  received_data_length--;
  chunk_state.bytes --;

  if (b == Z80_ESCAPE) {
    current_state = &s_chunk_repcount;
  } else {
    /*
     * False alarm: the escape byte was followed by a non-escape byte,
     *              so this is not a compressed sequence
     */
    *curr_write_pos++ = Z80_ESCAPE;
    rep_state.plain_byte = b;

    if (IS_KILOBYTE(curr_write_pos)) {
      update_progress();
    }

    current_state = &s_chunk_single_escape;
  }
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_single_escape)
{
  *curr_write_pos++ = rep_state.plain_byte;

  if (IS_KILOBYTE(curr_write_pos)) {
    update_progress();
  }

  current_state = &s_chunk_compressed;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repcount)
{
  rep_state.count = *received_data++;
  received_data_length--;
  chunk_state.bytes --;

  current_state = &s_chunk_repvalue;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repvalue)
{
  rep_value = *received_data++;
  received_data_length--;
  chunk_state.bytes --;

  current_state = &s_chunk_repetition;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repetition)
{
  __asm

  ld  a, (_rep_state)
  ld  b, a                      ;; loop counter rep_state.count
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
  ld  (_rep_state), a
  ld  (_curr_write_pos), hl

  call  _update_progress
  jr  s_chunk_repetition_done

s_chunk_repetition_write_back::
  ld  (_rep_state), a           ;; copied from b above
  ld  (_curr_write_pos), hl

  ld  a, #<_s_chunk_compressed
  ld  (_current_state), a
  ld  a, #>_s_chunk_compressed
  ld  (_current_state+1), a

s_chunk_repetition_done::

__endasm;
}

/* ------------------------------------------------------------------------- */

#ifndef SB_MINIMAL

/*
 * State RAW_DATA:
 *
 * Simply copies received data to the indicated location.
 */
DEFINE_STATE(s_raw_data)
{
  __asm

  ld  de, (_curr_write_pos)
  ld  hl, (_received_data)
  ld  bc, (_received_data_length)
  ldir
  ld  (_curr_write_pos), de
  ld  (_received_data_length), bc

  __endasm;
}

#endif

/* ========================================================================= */

void
receive_file_data(void)
{
  /* Indicates an evacuation is ongoing (see below) */
  static bool evacuating = false;

  received_data        = TFTP_DATA_BUFFER;
  received_data_length = ntohs(rx_frame.udp.header.length)
                         - sizeof(struct udp_header_t)
                         - sizeof(struct tftp_header_t);

  while (received_data_length != 0) {
    if ((LOBYTE(curr_write_pos) == 0)
#ifndef SB_MINIMAL
    && (current_state != &s_raw_data)
#endif
    ) {

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

#ifndef SB_MINIMAL
  /*
   * If we received a TFTP frame with less than the maximal payload, it must
   * be the last one. If it was a snapshot, we would not come here (but
   * context-switched instead). Therefore, we must now have finished loading
   * the snapshot list. NUL-terminate the list, prepare for loading a
   * snapshot, and run the menu.
   */
  if (ntohs(rx_frame.udp.header.length) < (TFTP_DATA_MAXSIZE
					   + sizeof(struct udp_header_t)
					   + sizeof(struct tftp_header_t)))
  {
    *curr_write_pos = 0;

    expect_snapshot();
    run_menu();
  }
#endif
}

/* ------------------------------------------------------------------------- */

#ifndef SB_MINIMAL
void
expect_snapshot(void)
{
  curr_write_pos = (uint8_t *) 0x4000;
  current_state  = &s_header;
}
#endif /* ifndef SB_MINIMAL */
