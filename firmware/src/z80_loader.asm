;;
;; Module z80_loader:
;;
;; Accepts a stream of bytes, unpacks it as a .z80 snapshot,
;; loads it into RAM, and executes it.
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

    .module z80_loader
    .optsdcc -mz80

    .include "include/z80_loader.inc"

    .include "include/context_switch.inc"
    .include "include/enc28j60.inc"
    .include "include/eth.inc"
    .include "include/globals.inc"
    .include "include/spi.inc"
    .include "include/tftp.inc"
    .include "include/udp_ip.inc"
    .include "include/ui.inc"
    .include "include/util.inc"

;; ============================================================================

Z80_ESCAPE         = 0xED     ;; escape byte in compressed chunks
ATTR_DIGIT_ROW     = 0x5A00   ;; attribute VRAM address for kilobyte counter

PROGRESS_BAR_BASE  = ATTRS_BASE + 0x2E0


;; ============================================================================

    .area _DATA

;; ----------------------------------------------------------------------------
;; expected and currently loaded no. of kilobytes, for progress display
;; ----------------------------------------------------------------------------

_kilobytes_loaded:
    .ds    1

;; ============================================================================

;; ----------------------------------------------------------------------------
;; The Z80 snapshot state machine is implemented by one routine for each
;; state. The function returns whenever one of the following happens:
;;
;; - all currently available data has been consumed (received_data_length == 0)
;; - a state transition is required
;; - the write pointer has reached an integral number of kilobytes
;;   (the outer loop then manages evacuation)
;; ----------------------------------------------------------------------------

    .area _STAGE2

;; ############################################################################
;; show_attr_digit
;;
;; subroutine: show huge digit in attributes, on row ATTR_DIGIT_ROW and down
;; L: column (0..31)
;; A: digit (0..9), bits 4-7 are ignored
;;
;; Destroys AF, B, DE, HL. Returns with L increased by 7.
;; ############################################################################

ROM_DIGITS = 0x3D00 + 16 * 8 + 1 ;; address of '0' bits, first actual scan line

show_attr_digit:

    add   a, a
    add   a, a
    add   a, a

show_attr_digit_already_shifted:  ;; special target for below

    and   a, #0x78           ;; binary 01111000
    add   a, #<ROM_DIGITS    ;; all digits in a single 256b page
    ld    d, #>ROM_DIGITS
    ld    e, a

    ld    h, #>ATTR_DIGIT_ROW

    di    ;; SpeccyBoot is about to be paged out

    ld    a, #SPI_IDLE+SPI_CS+PAGE_OUT
    out   (SPI_OUT), a

show_attr_char_address_known:
00001$:

    ld    a, (de)
    inc   de
    add   a, a
    ld    b, #6
00002$:
    add   a, a
    jr    c, 00004$
    ld    (hl), #WHITE + (WHITE << 3)
    .db   JP_C        ;; C always clear here => ignore the following two bytes
00004$:
    ld    (hl), #BLACK + (BLACK << 3)
00003$:
    inc   hl
    djnz  00002$

    ld    a, #(ROW_LENGTH-6)
    add   a, l
    ld    l, a

    cp    a, #ROW_LENGTH * 6
    jr    c, 00001$

    ld    a, #SPI_IDLE+SPI_CS       ;; page SpeccyBoot back in
    out   (SPI_OUT), a
    ei

    ret


;; ############################################################################
;; load_byte_from_chunk
;;
;; like load_byte_from_packet, but also decreases BC
;; ############################################################################

    .area _CODE

load_byte_from_chunk:

    dec   bc

    ;; FALL THROUGH to load_byte_from_packet


;; ############################################################################
;; load_byte_from_packet
;; ############################################################################

load_byte_from_packet:

    ld   a, (iy)
    inc  iy

    dec  hl

    ret

;; ############################################################################
;; State HEADER (initial):
;;
;; Evacuates the header from the TFTP data block. Returns the length of the
;; header (i.e., the offset of snapshot data within the TFTP data block)
;;
;; This function does some header parsing; it initializes compression_method
;; and verifies compatibility.
;; ############################################################################

    .area _STAGE2

s_header:

    push de
    push hl

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until prepare_context is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #_snapshot_header

    ;; BC is set to Z80_HEADER_RESIDENT_SIZE in z80_loader_receive_hook below

    ldir

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
    ;; Not an extended header: expect a single 48k chunk. For a compressed
    ;; chunk this value will be overkill (the compressed chunk is actually
    ;; shorter). This is OK, since the context switch will kick in when the
    ;; chunk is fully loaded anyway.
    ;; ------------------------------------------------------------------------

    ld   b, #0xC0   ;; BC == 0 from LDIR above, so BC == 0xC000 now

    ;; ------------------------------------------------------------------------
    ;; decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK

    ;; A is zero here (as expected by set_state_uncompressed)

    call z, set_state_uncompressed
    call nz, set_state_compressed
    jr   s_header_set_state

s_header_ext_hdr:

    ;; ------------------------------------------------------------------------
    ;; extended header: adjust expected no. of kilobytes for a 128k snapshot
    ;; ------------------------------------------------------------------------

    ld    a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_HW_TYPE)
    cp    a, #SNAPSHOT_128K
    jr    c, s_header_not_128k
    ld    a, #128
    ld    (kilobytes_expected), a
    xor   a, a
    ld    (progress_add_instr), a
    ld    a, #4
    ld    (progress_ratio), a

s_header_not_128k:

    ;; ------------------------------------------------------------------------
    ;; adjust header length, keep in DE
    ;; ------------------------------------------------------------------------

    ld    hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_EXT_LENGTH)
    add   hl, de
    inc   hl
    inc   hl
    ex    de, hl

    ;; ------------------------------------------------------------------------
    ;; a chunk is expected next
    ;; ------------------------------------------------------------------------

    ld   ix, #_s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; adjust IY and HL for header size
    ;; ------------------------------------------------------------------------

    pop  hl

    add  iy, de         ;; clears C flag, as DE is less than IY here
    sbc  hl, de         ;; C flag is zero here

    pop  de

    ret


;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive first byte in chunk header: low byte of chunk length
;; ############################################################################

    .area _STAGE2

_s_chunk_header:

    call load_byte_from_packet
    ld   c, a

    ld   ix, #_s_chunk_header2

    ret


;; ############################################################################
;; state CHUNK_HEADER2:
;;
;; receive second byte in chunk header: high byte of chunk length
;; ############################################################################

    .area _STAGE2

_s_chunk_header2:

    call load_byte_from_packet
    ld   b, a

    ld   ix, #_s_chunk_header3

    ret

;; ############################################################################
;; state CHUNK_HEADER3:
;;
;; receive third byte in chunk header:
;; ID of the page the chunk belongs to (range is 3..10)
;;
;; See:
;; https://www.worldofspectrum.org/faq/reference/z80format.htm
;; https://www.worldofspectrum.org/faq/reference/128kreference.htm#ZX128Memory
;; ############################################################################

    .area _STAGE2

_s_chunk_header3:

    ;; use DE for scratch here: it will get its proper value below

    ld   a, (_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    ld   e, a

    call load_byte_from_packet

    cp   a, #3
    jr   c, s_chunk_header3_incompatible
    cp   a, #11
    jr   c, s_chunk_header3_compatible
s_chunk_header3_incompatible:
    ld   a, #FATAL_INCOMPATIBLE
    jp   fail
s_chunk_header3_compatible:

    ;; Decide on a good value for write_pos; store in DE.

    ;;
    ;; Need to handle page 5 separately -- if we do not use the address range
    ;; 0x4000..0x7fff, the evacuation stuff will not work.
    ;;

    cp   a, #8                       ;; means page 5 (0x4000..0x7fff)
    ld   d, #0x40
    jr   z, s_chunk_header3_set_page

    ;;
    ;; Page 1 is handled differently in 48k and 128k snapshots:
    ;; https://worldofspectrum.org/faq/reference/z80format.htm
    ;;
    ;; In a 48k snapshot, page 1 is loaded at 0x8000.
    ;; In a 128k snapshot, page 1 is loaded at 0xc800, and the
    ;; memory configuration set accordingly.
    ;;

    cp   a, #4                       ;; means page 1 (0x8000..0xbfff)
    ld   d, #0x80
    ld   a, e
    jr   nz, s_chunk_header3_default_page

    cp   a, #SNAPSHOT_128K               ;; is this a 128k snapshot?
    jr   c, s_chunk_header3_set_page     ;; if not, we are done here

s_chunk_header3_default_page:

    ;;
    ;; This is either
    ;;   (a) page 5 in a 48k snapshot, or
    ;;   (b) a page in a 128k snapshot
    ;;
    ;; In either case, the page is set up to be loaded at 0xc000.
    ;;

    cp   a, #SNAPSHOT_128K
    ld   d, #0xc0
    jr   c, s_chunk_header3_set_page

    ;; If this is a 128k snapshot, switch memory bank

    ld   a, -1(iy)        ;; byte just loaded: chunk bank id
    sub  a, #3
    or   a, #MEMCFG_ROM_48K    ;; needed for digits while loading

    exx
    ld   bc, #MEMCFG_ADDR
    out  (c), a
    exx

s_chunk_header3_set_page:

    ld   e, #0

    ;; -----------------------------------------------------------------------
    ;; https://worldofspectrum.org/faq/reference/z80format.htm :
    ;;
    ;; If length=0xffff, data is 16384 bytes long and not compressed
    ;; -----------------------------------------------------------------------

    ld   a, b
    and  a, c
    inc  a
    jr   nz, set_state_compressed

    ld   bc, #0x4000

    ;; NOTE: A is zero here (from the INC A) above, as expected
    ;; by set_state_uncompressed

    ;; FALL THROUGH, but skip two bytes of set_state_compressed
    ;; so as to fall through to set_state_uncompressed

    .db   JP_NZ      ;; Z is set from INC A, so skip the following two bytes

;; ############################################################################
;; set_state_compressed
;; set_state_uncompressed
;;
;; When calling set_state_uncompressed, A must be zero. These functions do not
;; affect any CPU flags.
;;
;; These functions patch the JR Z branch at escape_check_branch.
;; For compressed data, the offset is set to branch to escape_check_branch.
;; For uncompressed data, the offset is set to zero, to ensure that Z80_ESCAPE
;; bytes are handled like any others.
;; ############################################################################

set_state_compressed:

    ;; rewrite JR Z branch for compressed chunks (to react to Z80_ESCAPE bytes)

    ld    a, #chunk_escape - escape_check_branch - 2

    ;; FALL THROUGH

;; ############################################################################
;; set_state_uncompressed
;; ############################################################################

set_state_uncompressed:

    ld    (escape_check_branch + 1), a
    ld    ix, #s_chunk_write_data

    ret


;; ############################################################################
;; state CHUNK_REPCOUNT
;; ############################################################################

    .area _STAGE2

_s_chunk_repcount:

    call load_byte_from_chunk

    ld   (_repcount), a
    ld   ix, #_s_chunk_repvalue

    ret


;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _STAGE2

_s_chunk_repvalue:

    call load_byte_from_chunk
    ld   i, a

    ld   ix, #s_chunk_write_data

    ;; FALL THROUGH to s_chunk_write_data


;; ############################################################################
;; state CHUNK_WRITE_DATA
;; ############################################################################

s_chunk_write_data:

  ;; -------------------------------------------------------------------------
  ;; Check the repetition count. This is zero when no repetition is active,
  ;; including when an uncompressed chunk is being loaded.
  ;; -------------------------------------------------------------------------

  .db  LD_A_N             ;; LD A, #n
_repcount:
  .db  0

  or   a, a
  jr   nz, do_repetition

  ;; -------------------------------------------------------------------------
  ;; reached end of current chunk?
  ;; -------------------------------------------------------------------------

  ld   a, b
  or   a, c
  jr   z, chunk_done

  ;; -------------------------------------------------------------------------
  ;; reached end of loaded TFTP data?
  ;; -------------------------------------------------------------------------

  ld   a, h
  or   a, l
  ret  z

  ld   a, (iy)
  inc  iy

  dec  bc
  dec  hl

  ;; -------------------------------------------------------------------------
  ;; Check for the escape byte of a repetition sequence. If an uncompressed
  ;; chunk is being loaded, the branch 'escape_check_branch' below is
  ;; patched to a JR Z, 0, to ensure escape bytes are handled just like
  ;; any other.
  ;; -------------------------------------------------------------------------

  cp   a, #Z80_ESCAPE

escape_check_branch:
  jr   z, chunk_escape

store_byte:

  ld   (de), a
  inc  de

  ;; -------------------------------------------------------------------------
  ;; update progress if DE reached a kilobyte boundary
  ;; -------------------------------------------------------------------------

  ld   a, d
  and  a, #0x03
  or   a, e
  jr   nz, s_chunk_write_data

  jr   update_progress

  ;; -------------------------------------------------------------------------
  ;; a non-zero number of repetitions remain:
  ;; decrease repetition count and write the repetition value to memory
  ;; -------------------------------------------------------------------------

do_repetition:

  dec  a
  ld   (_repcount), a

  ld   a, i

  jr   store_byte

  ;; -------------------------------------------------------------------------
  ;; End of current chunk: switch state
  ;; -------------------------------------------------------------------------

chunk_done:
  ld   ix, #_s_chunk_header
  ret

  ;; -------------------------------------------------------------------------
  ;; Escape byte found: switch state
  ;; -------------------------------------------------------------------------

chunk_escape:
  ld   ix, #_s_chunk_compressed_escape
  ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

    .area _STAGE2

_s_chunk_compressed_escape:

    call  load_byte_from_packet
    dec   bc

    ld    ix, #_s_chunk_repcount        ;; tentative next state

    cp    a, #Z80_ESCAPE
    ret   z

    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not an escape sequence
    ;;

    ex    af, af'            ;; '

    ld    a, #Z80_ESCAPE
    ld    (de), a
    inc   de

    call  update_progress

    ex    af, af'            ;; '

    ld    (de), a
    inc   de

    ld    ix, #s_chunk_write_data

    ;; FALL THROUGH to update_progress


;; ############################################################################
;; update_progress
;;
;; If the number of bytes loaded reached an even kilobyte,
;; increase kilobyte counter and update status display
;; ############################################################################

update_progress:

    ;; check if DE is an integral number of kilobytes,
    ;; return early otherwise

    ld    a, d
    and   a, #0x03
    or    a, e
    ret   nz

    ;; ------------------------------------------------------------------------
    ;; use alternate BC, DE, HL for scratch here
    ;; ------------------------------------------------------------------------

    exx

    ;; ========================================================================
    ;; update the progress display
    ;; ========================================================================

    ;; This instruction is patched with different values at runtime.

    .db   LD_A_N
_digits:
    .db   0      ;; digits (BCD) for progress display while loading a snapshot

    inc   a
    daa
    push  af             ;; remember flags
    ld    (_digits), a
    ld    c, a
    jr    nz, not_100k   ;; turned from 99->100?

    ;; Number of kilobytes became zero in BCD:
    ;; means it just turned from 99 to 100.
    ;; Print the digit '1' for hundreds.

    ld    l, a
    inc   a      ;; A is now 1
    call  show_attr_digit
    ld    a, c

not_100k:
    pop   de             ;; recall flags, old F is now in E
    bit   #4, e          ;; was H flag set? Then the tens have increased
    jr    z, not_10k

    ;; Print tens (_x_)

    rra
    ld    l, #7
    call  show_attr_digit_already_shifted

not_10k:
    ;; Print single-number digit (__x)

    ld    a, c
    ld    l, #14
    call  show_attr_digit

    ;; ************************************************************************
    ;; update progress bar
    ;; ************************************************************************

    ld    hl, #_kilobytes_loaded
    inc   (hl)
    ld    a, (hl)

    ;; ------------------------------------------------------------------------
    ;; Scale loaded number of kilobytes to a value 0..32. By default, 48k
    ;; snapshots are scaled * 2 / 3. For 128k snapshots, the add below is
    ;; patched to a NOP, and the immediate constant for LD_B_N to 4.
    ;; ------------------------------------------------------------------------

progress_add_instr:
    add   a, a       ;; patched to NOP for 128k snapshots

    .db   LD_B_N
progress_ratio:
    .db   3          ;; patched to 4 for 128k snapshots

    call  a_div_b
    ld    a, c

00002$:
    or    a, a
    jr    z, no_progress_bar

    ld    bc, #PROGRESS_BAR_BASE-1
    add   a, c
    ld    c, a
    ld    a, #(GREEN + (GREEN << 3))
    ld    (bc), a

    ;; ========================================================================
    ;; if all data has been loaded, perform the context switch
    ;; ========================================================================

    .db   LD_A_N
kilobytes_expected:
    .db   48         ;; initial assumption, possibly patched to 128 in s_header

    cp    a, (hl)
    jp    z, context_switch             ;; in stage 1 loader (ROM)

no_progress_bar:

    exx
    ret


;; ############################################################################
;; z80_loader_receive_hook
;; ############################################################################

    .area _STAGE2

z80_loader_receive_hook:

    ;; ------------------------------------------------------------------------
    ;; register allocation (shared in all states):
    ;;
    ;; IX: current state (pointer to function)
    ;; IY: read pointer (pointer to somewhere in rx_frame)
    ;; BC: number of bytes left to read in current chunk       (Bytes in Chunk)
    ;; DE: write pointer (pointer to somewhere in RAM)            (DEstination)
    ;; HL: number of bytes left to read from current TFTP packet
    ;; I:  repetition value (valid during an ED ED repetition)
    ;; ------------------------------------------------------------------------

    .dw  LD_IX_NN            ;; LD IX, #nn
z80_loader_state:
    .dw  s_header            ;; initial state

    ld   iy, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE

    ;; ------------------------------------------------------------------------
    ;; set up HL, BC, DE
    ;; ------------------------------------------------------------------------

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_OFFSETOF_LENGTH)
    ld   a, l      ;; byteswap: length is stored in network order in UDP header
    ld   l, h
    ld   h, a
    ld   bc, #0x10000 - UDP_HEADER_SIZE - TFTP_HEADER_SIZE
    add  hl, bc

    .db  LD_BC_NN          ;; LD BC, #nn
_chunk_bytes_remaining:
    .dw  Z80_HEADER_RESIDENT_SIZE    ;; useful initial value for state S_HEADER

    .db  LD_DE_NN          ;; LD DE, #nn
write_pos:
    .dw  0x4000            ;; default for single-chunk snapshots

    ;; ========================================================================
    ;; read bytes, evacuate when needed, call state functions
    ;; ========================================================================

receive_snapshot_byte_loop:

    ;; ------------------------------------------------------------------------
    ;; if HL is zero, we are done
    ;; ------------------------------------------------------------------------

    ld    a, h
    or    a, l
    ret   z

    ;; ------------------------------------------------------------------------
    ;; check evacuation status only if low byte of E is zero
    ;; ------------------------------------------------------------------------

    ld    a, e
    or    a, a
    jr    nz, receive_snapshot_no_evacuation

    ld    a, d     ;; A is now high byte of write_pos

    ;; ------------------------------------------------------------------------
    ;; reached RUNTIME_DATA (resident area)?
    ;; ------------------------------------------------------------------------

    cp    a, #>RUNTIME_DATA
    jr    nz, receive_snapshot_not_entering_runtime_data

    ;; ------------------------------------------------------------------------
    ;; then store data in EVACUATION_TEMP_BUFFER instead,
    ;; and enable evacuation
    ;; ------------------------------------------------------------------------

    ld    d, #>EVACUATION_TEMP_BUFFER
    ld    a, #JR_NZ                                ;; evacuation is now enabled
    ld    (evacuation_activation_instr), a

    jr    receive_snapshot_no_evacuation

receive_snapshot_not_entering_runtime_data:

    ;; ------------------------------------------------------------------------
    ;; is an evacuation about to be completed?
    ;; ------------------------------------------------------------------------

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
evacuation_activation_instr:
    jr    receive_snapshot_no_evacuation

    ;; ------------------------------------------------------------------------
    ;; then set write_pos := RUNTIME_DATA + RUNTIME_DATA_LENGTH,
    ;; and disable evacuation
    ;; ------------------------------------------------------------------------

    ld    d, #>(RUNTIME_DATA + RUNTIME_DATA_LENGTH)

    ;; evacuation (soon) done: change JR above to skip evacuation next time
    ld    a, #JR_UNCONDITIONAL
    ld    (evacuation_activation_instr), a

    ;; ------------------------------------------------------------------------
    ;; prepare context switch and copy the evacuated data to ENC28J60 RAM
    ;; ------------------------------------------------------------------------

    call  prepare_context

receive_snapshot_no_evacuation:

    ;; ------------------------------------------------------------------------
    ;; call function pointed to by z80_loader_state
    ;; there is no "CALL (IX)" instruction, so CALL a JP (IX) instead
    ;; ------------------------------------------------------------------------

    call  jp_ix_instr

    ld    (z80_loader_state), ix
    ld    (_chunk_bytes_remaining), bc
    ld    (write_pos), de

    jr    receive_snapshot_byte_loop

jp_ix_instr:
    jp    (ix)