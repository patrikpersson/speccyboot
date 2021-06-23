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

    .include "z80_loader.inc"

    .include "context_switch.inc"
    .include "enc28j60.inc"
    .include "eth.inc"
    .include "globals.inc"
    .include "spi.inc"
    .include "tftp.inc"
    .include "udp_ip.inc"
    .include "util.inc"

;; ============================================================================

Z80_ESCAPE         = 0xED     ;; escape byte in compressed chunks
ATTR_DIGIT_ROW     = 0x5A00   ;; attribute VRAM address for kilobyte counter

PROGRESS_BAR_BASE  = ATTRS_BASE + 0x2E0


;; ============================================================================

;; ----------------------------------------------------------------------------
;; The Z80 snapshot state machine is implemented by one routine for each
;; state. The function returns whenever one of the following happens:
;;
;; - all currently available data has been consumed (HL == 0)
;; - a state transition is required
;; - the DE write pointer has reached an integral number of kilobytes
;; ----------------------------------------------------------------------------
;;
;; States:
;;  
;;                        HEADER                       (parse snapshot header)
;;                           |
;; (for v.1 snapshots) /-----+-----\ (for v.2+ snapshots)
;;                     |           |
;;                     |           v  
;;                     |      CHUNK_HEADER <--\        (load low byte
;;                     |           |          |         of chunk length)
;;                     v           v          |
;;                     |      CHUNK_HEADER2   |        (load high byte
;;                     |           |          |         of chunk length)
;;                     |           v          ^
;;                     |      CHUNK_HEADER3   |        (load byte: page id)
;;                     |           |          |
;;                     \-----+-----/          |
;;                           |                |
;;                           v                |
;;          /-------> CHUNK_WRITE_DATA --->---/        (write bytes to RAM)
;;          |            |        ^
;;          |            v        |
;;          ^      CHUNK_COMPRESSED_ESCAPE             (handle ED ED
;;          |                 |                         compressed sequences)
;;          |                 v
;;     CHUNK_REPVAL <-- CHUNK_REPCOUNT
;;
;; ----------------------------------------------------------------------------

;; ############################################################################
;; show_attr_digit
;;
;; subroutine: show huge digit in attributes, on row ATTR_DIGIT_ROW and down
;; L: column (0..31)
;; A: digit (0..9), bits 4-7 are ignored
;;
;; Destroys AF, B, DE, HL. Returns with L increased by 7.
;; ############################################################################

    .area _CODE

show_attr_digit:

    add   a, a
    add   a, a
    add   a, a

show_attr_digit_already_shifted:  ;; special target for below

    and   a, #0x78                ;; binary 01111000
    add   a, #<digit_font_data    ;; all digits in a single 256b page
    ld    d, #>digit_font_data
    ld    e, a

    ld    h, #>ATTR_DIGIT_ROW

show_attr_char_address_known:

    ld    a, (de)
    inc   de
    add   a, a
    ld    b, #6
show_attr_char_pixel_loop:
    add   a, a
    jr    c, show_attr_char_pixel_set
    ld    (hl), #WHITE + (WHITE << 3)
    .db   JP_C        ;; C always clear here => ignore the following two bytes
show_attr_char_pixel_set:
    ld    (hl), #BLACK + (BLACK << 3)
    inc   hl
    djnz  show_attr_char_pixel_loop

    ld    a, #(ROW_LENGTH-6)
    add   a, l
    ld    l, a

    cp    a, #ROW_LENGTH * 6
    jr    c, show_attr_char_address_known

    ret


;; ############################################################################
;; update_progress
;;
;; If the number of bytes loaded reached an even kilobyte,
;; increase kilobyte counter and update status display
;; ############################################################################

    .area _CODE

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

    ld    hl, #_digits
    ld    a, (hl)
    inc   a
    daa
    push  af             ;; remember flags
    ld    (hl), a
    ld    c, a

    ;; If Z is set, then the number of kilobytes became zero in BCD:
    ;; means it just turned from 99 to 100.
    ;; Print the digit '1' for hundreds.

    ld    l, a       ;; L := 0
    ld    a, #1      ;; can't use INC A, since that changes Z flag
    call  z, show_attr_digit
    ld    a, c

not_100k:
    pop   de             ;; recall flags, old F is now in E
    bit   #4, e          ;; was H flag set? Then the tens have increased
    jr    z, not_10k

    ;; Print tens (_x_)

    rra                  ;; shift once; routine below shifts three more times
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

    ;; kilobytes_loaded is located directly after kilobytes_expected, so
    ;; use a single pointer

    ld    hl, #kilobytes_expected

    ld    a, (hl)                      ;; load kilobytes_expected
    inc   hl                           ;; now points to kilobytes_loaded
    inc   (hl)                         ;; increase kilobytes_loaded
    add   a, a                         ;; sets carry if this is a 128k snapshot
    ld    a, (hl)                      ;; kilobytes_loaded

    ;; ------------------------------------------------------------------------
    ;; Scale loaded number of kilobytes to a value 0..32.
    ;; 48k snapshots:   * 2 / 3
    ;; 128k snapshots:  * 1 / 4 
    ;; ------------------------------------------------------------------------

    ld    b, #4

    jr    c, progress_128

    add   a, a       ;; 48 snapshot: * 2 / 3
    dec   b

progress_128:

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

    ld    a, (hl)
    dec   hl
    cp    a, (hl)
    jp    z, context_switch

no_progress_bar:

    exx

    ;; ========================================================================
    ;; handle evacuation of resident data (0x5800..0x5fff)
    ;; ========================================================================

    ld    a, d

    cp    a, #>RUNTIME_DATA
    jp    z, start_storing_runtime_data

    cp    a, #>(EVACUATION_TEMP_BUFFER + RUNTIME_DATA_LENGTH)
    ret   nz

    ;; ------------------------------------------------------------------------
    ;; use register R, bit 7 to indicate whether the evacuation is already
    ;; done (R==0 after reset)
    ;; ------------------------------------------------------------------------

    ld    a, r
    ret   m          ;; return if R bit 7 is 1
    cpl              ;; R bit 7 was 0, is now 1
    ld    r, a

    ;; ------------------------------------------------------------------------
    ;; use alternate BC, DE, HL for scratch here
    ;; ------------------------------------------------------------------------

    exx
    prepare_context_switch
    exx

start_storing_runtime_data:
    ld    d, #>EVACUATION_TEMP_BUFFER
    ret


;; ############################################################################
;; load_byte_from_chunk
;;
;; like load_byte_from_packet, but also decreases HL
;; ############################################################################

    .area _CODE

load_byte_from_chunk:

    dec   hl

    ;; FALL THROUGH to load_byte_from_packet


;; ############################################################################
;; load_byte_from_packet
;; ############################################################################

load_byte_from_packet:

    ld   a, (iy)
    inc  iy

    dec  bc

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

    .area _NONRESIDENT

s_header:

    push bc

    ;; ------------------------------------------------------------------------
    ;; keep .z80 header until prepare_context is called
    ;; ------------------------------------------------------------------------

    ld   hl, #_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE
    ld   de, #stored_snapshot_header
    ld   bc, #Z80_HEADER_RESIDENT_SIZE

    ldir

    ;; ------------------------------------------------------------------------
    ;; check snapshot header
    ;; ------------------------------------------------------------------------

    ld    a, #48                  ;; initial assumption, possibly revised below
    ld    (kilobytes_expected), a

    ;; set DE to .z80 snapshot header size
    ;; (initially the snapshot v1 size, modified later below)

    ld   de, #Z80_HEADER_OFFSET_EXT_LENGTH

    ld   hl, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_PC)
    ld   a, h
    or   a, l
    jr   z, s_header_ext_hdr               ;; extended header?

    ;; ------------------------------------------------------------------------
    ;; Assume a single 48k chunk.
    ;; Decide next state, depending on whether COMPRESSED flag is set
    ;; ------------------------------------------------------------------------

    ld   a, (_rx_frame + IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + Z80_HEADER_OFFSET_MISC_FLAGS)
    and  a, #SNAPSHOT_FLAGS_COMPRESSED_MASK

    ;; if Z is set, then A is zero (as expected by set_state_uncompressed)

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

    ld   ix, #s_chunk_header

s_header_set_state:

    ;; ------------------------------------------------------------------------
    ;; adjust IY and BC for header size
    ;; ------------------------------------------------------------------------

    pop  hl            ;; was pushed as BC above

    add  iy, de        ;; clears C flag (as IY > DE)
    sbc  hl, de        ;; so no carry here

    ld   b, h
    ld   c, l

    ;; ------------------------------------------------------------------------
    ;; Set up register defaults for a single 48k chunk. For a version 2+
    ;; snapshot these values will be superseded in the chunk header.
    ;; ------------------------------------------------------------------------

    ld   hl, #0xC000
    ld   de, #0x4000

    ret


;; ############################################################################
;; state CHUNK_HEADER:
;;
;; receive first byte in chunk header: low byte of chunk length
;; ############################################################################

    .area _STAGE2

s_chunk_header:

    call load_byte_from_packet
    ld   l, a

    ld   ix, #s_chunk_header2

    ret


;; ############################################################################
;; state CHUNK_HEADER2:
;;
;; receive second byte in chunk header: high byte of chunk length
;; ############################################################################

    .area _STAGE2

s_chunk_header2:

    call load_byte_from_packet
    ld   h, a

    ld   ix, #s_chunk_header3

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

s_chunk_header3:

    ;; use DE for scratch here: it will get its proper value below

    ld   a, (stored_snapshot_header + Z80_HEADER_OFFSET_HW_TYPE)
    ld   e, a

    call load_byte_from_packet

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
    ;; If chunk length=0xffff, data is 16384 bytes long and not compressed
    ;; -----------------------------------------------------------------------

    ld   a, h
    and  a, l
    inc  a
    jr   nz, set_state_compressed

    ld   hl, #0x4000

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

s_chunk_repcount:

    call load_byte_from_chunk

    ld   (_repcount), a
    ld   ix, #s_chunk_repvalue

    ret


;; ############################################################################
;; state CHUNK_REPVALUE
;; ############################################################################

    .area _STAGE2

s_chunk_repvalue:

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

  ld   a, h
  or   a, l
  jr   z, chunk_done

  ;; -------------------------------------------------------------------------
  ;; reached end of loaded TFTP data?
  ;; -------------------------------------------------------------------------

  ld   a, b
  or   a, c
  ret  z

  call load_byte_from_chunk

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

  jp   update_progress

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
  ld   ix, #s_chunk_header
  ret

  ;; -------------------------------------------------------------------------
  ;; Escape byte found: switch state
  ;; -------------------------------------------------------------------------

chunk_escape:
  ld   ix, #s_chunk_compressed_escape
  ret


;; ############################################################################
;; state CHUNK_COMPRESSED_ESCAPE
;; ############################################################################

    .area _STAGE2

s_chunk_compressed_escape:

    call  load_byte_from_chunk

    ld    ix, #s_chunk_repcount        ;; tentative next state

    cp    a, #Z80_ESCAPE
    ret   z

    ;;
    ;; False alarm: the escape byte was followed by a non-escape byte,
    ;;              so this is not an escape sequence
    ;;

    ex    af, af'

    ld    a, #Z80_ESCAPE
    ld    (de), a
    inc   de

    call  update_progress

    ex    af, af'

    ld    (de), a
    inc   de

    ld    ix, #s_chunk_write_data

    jp    update_progress
