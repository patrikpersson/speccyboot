/*
 * Module z80_parser:
 *
 * Accepts a stream of bytes, unpacks it as a Z80 snapshot, loads it into
 * RAM, and executes it.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
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

#include "z80_parser.h"
#include "context_switch.h"
#include "speccyboot.h"

#include "util.h"
#include "logging.h"

#include "enc28j60_spi.h"

/*
 * Size of a memory bank/page
 */
#define PAGE_SIZE                       (0x4000)

/*
 * Special value for 'bytes_remaining' indicating no compression used
 */
#define BANK_LENGTH_UNCOMPRESSED        (0xffff)

/* ------------------------------------------------------------------------- */

/*
 * Masks for meaning of snapshot_flags
 */
#define SNAPSHOT_FLAGS_R7_MASK          (0x01)
#define SNAPSHOT_FLAGS_SAMROM_MASK      (0x10)
#define SNAPSHOT_FLAGS_COMPRESSED_MASK  (0x20)

#define SNAPSHOT_FLAGS_BORDER(f)        (((f) >> 1) & 0x07)

/*
 * The only value for hw_type we support
 */
#define HW_TYPE_SPECTRUM_48K            (0)

#define Z80_ESCAPE                      (0xED)

/* ------------------------------------------------------------------------- */

#define Z80_SNAPSHOT_FILENAME           "speccyboot_stage2.z80"

/* ------------------------------------------------------------------------- */

/*
 * Current position to write to (starts in video RAM)
 */
static uint8_t *curr_write_pos = (uint8_t *) 0x4000;

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

/*
 * State for a repetition sequence
 */
static union {
  uint8_t plain_byte;   /* set: s_chunk_single_escape read: s_chunk_single_escape*/
  uint8_t count;        /* set: chunk_compressed_repcount read: chunk_compressed_repetition */
} rep_state;

/*
 * Byte value for repetition
 */
static uint8_t rep_value;

/*
 * Pointer to received TFTP data
 */
static const uint8_t *received_data;

/*
 * Number of valid bytes remaining in received_data
 */
static uint16_t received_data_length;

/*
 * For progress display
 */
static uint8_t kilobytes_loaded = 0;

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

/*
 * Define next state
 */
#define SET_NEXT_STATE(s)               current_state = &s

/*
 * Determine how many bytes are left to the next integral number of 
 * kilobytes from a pointer. If the pointer currently points to an even
 * kilobyte, the distance to the next is returned (i.e., 0x0400).
 */
#define LEFT_TO_NEXT_KILOBYTE(p) \
  (((((uint16_t) (p)) + 0x0400) & 0xfc00) - (p))

/*
 * True if p is an integral number of kilobytes
 */
#define IS_KILOBYTE(p) \
  ((LOBYTE(p)) == 0 && ((HIBYTE(p) & 0x03) == 0))

#define MIN(a,b)                        ((a) < (b) ? (a) : (b))
#define MIN3(a,b,c)                     MIN(a, MIN(b, c))

/*
 * Syntactic sugar for declaring and defining states.
 */
#define DECLARE_STATE(s)                static void s (void)
#define DEFINE_STATE(s)                 DECLARE_STATE(s)

typedef void state_func_t(void);

/* ========================================================================= */

/*
 * State functions
 *
 * HEADER                       expecting header (initial state)
 *
 * CHUNK_UNCOMPRESSED           expecting uncompressed data
 * CHUNK_COMPRESSED             expecting compressed data
 *
 * CHUNK_HEADER                 expecting 3-byte header for a memory page
 * CHUNK_UNCOMPRESSED           expecting uncompressed memory page
 * CHUNK_COMPRESSED             expecting compressed memory page
 */
DECLARE_STATE(s_header);

DECLARE_STATE(s_chunk_uncompressed);
DECLARE_STATE(s_chunk_compressed);

DECLARE_STATE(s_chunk_header);
DECLARE_STATE(s_chunk_header2);
DECLARE_STATE(s_chunk_header3);

DECLARE_STATE(s_chunk_compressed_escape);
DECLARE_STATE(s_chunk_single_escape);
DECLARE_STATE(s_chunk_repcount);
DECLARE_STATE(s_chunk_repvalue);
DECLARE_STATE(s_chunk_repetition);

/* ------------------------------------------------------------------------- */

/*
 * We expect to receive a header before anything else
 */
static const state_func_t *current_state = &s_header;

/* ------------------------------------------------------------------------- */

/*
 * State HEADER (initial):
 *
 * Evacuates the header from the TFTP data block. Returns the length of the
 * header (i.e., the offset of snapshot data within the TFTP data block)
 *
 * This function does some header parsing; it initializes compression_method,
 * sets the border colour, and verifies compatibility.
 */
DEFINE_STATE(s_header)
{
  const struct z80_snapshot_header_extended_t *header
    = (const struct z80_snapshot_header_extended_t *) received_data;
  uint16_t header_length = sizeof(header->default_header);
  uint8_t          flags = header->default_header.snapshot_flags;
  
  if (header->default_header.pc != 0) {
    /*
     * Version 1 header found
     */
    if (flags & SNAPSHOT_FLAGS_SAMROM_MASK) {
      fatal_error(FATAL_ERROR_INCOMPATIBLE);
    }
    
    /*
     * Set expected number of bytes to 48k. A compressed chunk will terminate
     * earlier, but that is handled in the outer loop.
     */
    chunk_state.bytes = 0xc000;
    
    if (flags & SNAPSHOT_FLAGS_COMPRESSED_MASK) {
      SET_NEXT_STATE(s_chunk_compressed);
    }
    else {
      SET_NEXT_STATE(s_chunk_uncompressed);
    }
  }
  else {
    /*
     * Version 2 or version 3 header found
     */
    header_length += header->extended_header_bytes + 2;

    if (header->hw_type != HW_TYPE_SPECTRUM_48K) {
      fatal_error(FATAL_ERROR_INCOMPATIBLE);
    }

    SET_NEXT_STATE(s_chunk_header);
  }
  
  evacuate_z80_header(received_data);

  received_data        += header_length;
  received_data_length -= header_length;
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header)
{
  /*
   * Receive low byte of chunk length
   */
  chunk_state.bytes_lo = *received_data++;
  received_data_length--;
  
  SET_NEXT_STATE(s_chunk_header2);
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header2)
{
  /*
   * Receive high byte of chunk length
   */
  chunk_state.bytes_hi = *received_data++;
  received_data_length--;
  
  SET_NEXT_STATE(s_chunk_header3);
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_header3)
{
  /*
   * Receive ID of the page the chunk belongs to
   */
  received_data_length--;
  
  switch (*received_data++) {
    case 4:
      curr_write_pos = (uint8_t *) 0x8000;
      break;
    case 5:
      curr_write_pos = (uint8_t *) 0xc000;
      break;
    case 8:
      curr_write_pos = (uint8_t *) 0x4000;
      break;
    default:
      fatal_error(FATAL_ERROR_INCOMPATIBLE);
      break;
  }

  if (chunk_state.bytes == BANK_LENGTH_UNCOMPRESSED) {
    chunk_state.bytes = PAGE_SIZE;
    SET_NEXT_STATE(s_chunk_uncompressed);
  }
  else {
    SET_NEXT_STATE(s_chunk_compressed);
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
  add #4            ;; round up to next kilobyte
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
    
  ld  hl, #_kilobytes_loaded
  inc (hl)
  ld  a, (hl)
  push  af
  inc sp
  call  _display_digits
  inc sp

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
      
  ld  hl, #_kilobytes_loaded
  inc (hl)
  ld  a, (hl)
  push  af
  inc sp
  call  _display_digits
  inc sp
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
    SET_NEXT_STATE(s_chunk_repcount);
  } else {
    /*
     * False alarm: the escape byte was followed by a non-escape byte,
     *              so this is not a compressed sequence
     */
    *curr_write_pos++ = Z80_ESCAPE;
    rep_state.plain_byte = b;

    if (IS_KILOBYTE(curr_write_pos)) {
      display_digits(++ kilobytes_loaded);
    }
    
    SET_NEXT_STATE(s_chunk_single_escape);
  }
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_single_escape)
{
  *curr_write_pos++ = rep_state.plain_byte;
  
  if (IS_KILOBYTE(curr_write_pos)) {
    display_digits(++ kilobytes_loaded);
  }
  
  SET_NEXT_STATE(s_chunk_compressed);
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repcount)
{
  rep_state.count = *received_data++;
  received_data_length--;
  chunk_state.bytes --;
  
  SET_NEXT_STATE(s_chunk_repvalue);
}

/* ------------------------------------------------------------------------- */

DEFINE_STATE(s_chunk_repvalue)
{
  rep_value = *received_data++;
  received_data_length--;
  chunk_state.bytes --;
  
  SET_NEXT_STATE(s_chunk_repetition);
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
        
  ld  hl, #_kilobytes_loaded
  inc (hl)
  ld  a, (hl)
  push  af
  inc sp
  call  _display_digits
  inc sp
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

/* ========================================================================= */

/*
 * Called by TFTP (see tftp.h) when data is received
 */
void
z80_receive_data(const uint8_t *received_tftp_data,
                 uint16_t       nbr_of_bytes_received,
                 bool           more_data_expected)
{
  /*
   * Indicates an evacuation is ongoing (see below)
   */
  static bool evacuating = false;

  received_data        = received_tftp_data;
  received_data_length = nbr_of_bytes_received;
  
  while (received_data_length != 0) {
    
    if (LOBYTE(curr_write_pos) == 0) {
      if (kilobytes_loaded == 48) {    
        context_switch();
      }
      
      /*
       * Handle evacuation
       */
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
    
    /*
     * Execute code for current state
     */
    (*current_state)();
  }
  
  if (! more_data_expected) {
    fatal_error(FATAL_ERROR_END_OF_DATA);
  }
}
