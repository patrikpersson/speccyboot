/*
 * Module ui:
 *
 * Access to ZX Spectrum display, keyboard, and sound.
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

#include "ui.h"

#include "globals.h"

#pragma codeseg NONRESIDENT

/* ----------------------------------------------------------------------------
 * Keyboard mapping (used by _poll_key below)
 *
 * ZX Spectrum BASIC Programming (Vickers), Chapter 23:
 *
 * IN 65278 reads the half row CAPS SHIFT to V
 * IN 65022 reads the half row A to G
 * IN 64510 reads the half row Q to T
 * IN 63486 reads the half row 1 to 5
 * IN 61438 reads the half row O to 6
 * IN 57342 reads the half row P to 7
 * IN 49150 reads the half row ENTER to H
 * IN 32766 reads the half row SPACE to B
 *
 * http://www.worldofspectrum.org/ZXBasicManual/index.html
 *
 * A '0' in the 'key_rows' table means that key is to be ignored. The rows
 * are ordered for the high byte in the row address to take values in the
 * following order:
 *
 * 01111111
 * 10111111
 * 11011111
 * 11101111
 * 11110111
 * 11111011
 * 11111101
 * 11111110
 *-------------------------------------------------------------------------- */

static const uint8_t key_rows[] = {
  0x20, 0, 0x4d, 0x4e, 0x42,      /* 7FFE: space, shift, 'M', 'N', 'B' */
  0x0d, 0x4c, 0x4b, 0x4a, 0x48,   /* BFFE: enter, 'L', 'K', 'J', 'H' */
  0x50, 0x4f, 0x49, 0x55, 0x59,   /* DFFE: 'P', 'O', 'I', 'U', 'Y' */
  0x30, 0x39, 0x38, 0x37, 0x36,   /* EFFE: '0', '9', '8', '7', '6' */
  0x31, 0x32, 0x33, 0x34, 0x35,   /* F7FE: '1', '2', '3', '4', '5' */
  0x51, 0x57, 0x45, 0x52, 0x54,   /* FBDE: 'Q', 'W', 'E', 'R', 'T' */
  0x41, 0x53, 0x44, 0x46, 0x47,   /* FDFE: 'A', 'S', 'D', 'F', 'G' */
  0, 0x5a, 0x58, 0x43, 0x56,      /* FEFE: shift, 'Z', 'X', 'C', 'V' */
};

/* ------------------------------------------------------------------------- */

/*
 * Repeat time-outs: between the keypress and the first repetition, and for
 * any subsequent repetitions
 *
 * (measured in double-ticks of 20ms)
 */
#define REPEAT_FIRST_TIMEOUT    (40)
#define REPEAT_NEXT_TIMEOUT     (10)

#define REPEAT_NEXT_TIMEOUT     (10)

/* ------------------------------------------------------------------------- */

/*
 * Poll keyboard: return currently pressed key, or KEY_NONE, in register A.
 * The same value is also copied to register B.
 *
 * Destroys HL, BC, DE, AF.
 */
static void
poll_key(void)
__naked
{
  __asm

    ld    hl, #_key_rows
    ld    bc, #0x7ffe
poll_outer:
    in    d, (c)

    ld    e, #5       ;; number of keys in each row

poll_inner:
    ld    a, (hl)
    inc   hl
    rr    d
    jr    c, not_pressed
    or    a
    jr    nz, poll_done

not_pressed:
    dec   e
    jr    nz, poll_inner

    rrc   b
    jr    c, poll_outer

    xor   a         ;; KEY_NONE == 0

poll_done:
    ld    b, a

    ret

  __endasm;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void
print_at(uint8_t row,
         uint8_t start_col,
         char terminator,
         const char *s)
__naked
{
  (void) row, start_col, terminator, s;

  __asm

    ;; assume row             at (sp + 4)
    ;;        start_col       at (sp + 5)
    ;;        terminator      at (sp + 6)
    ;;        LOBYTE(s)       at (sp + 7)
    ;;        HIBYTE(s)       at (sp + 8)
    ;;
    ;; use:
    ;;
    ;; de = destination in VRAM
    ;; hl = pattern

    push  ix

    ld    ix, #0
    add   ix, sp

    ;; compute d as 0x40 + (row & 0x18)

    ld    a, 4(ix)   ;; row
    and   a, #0x18
    add   a, #0x40
    ld    d, a

    ;; compute e as ((row & 7) << 5) + start_col

    ld    a, 4(ix)  ;; row
    and   a, #7
    rrca            ;; rotate right by 3 == rotate left by 5, for values <= 7
    rrca
    rrca
    add   a, 5(ix)  ;; start_col
    ld    e, a

    ld    c, 7(ix)
    ld    b, 8(ix)

    push  bc
    pop   iy        ;; now holds s

string_loop::
    ld    a, e
    and   #0x1f     ;; current column
    cp    #0x1f     ;; right-most column
    jr    nc, print_done

    ld    a, (iy)
    cp    6(ix)     ;; terminator
    jr    z, string_padding
    inc   iy

    sub   a, #32
    ld    h, #0
    ld    l, a
    add   hl, hl
    add   hl, hl
    add   hl, hl
    ld    bc, #_font_data
    add   hl, bc

    ld    b, #8
    push  de
char_loop::
    ld    a, (hl)
    ld    (de), a
    inc   d
    inc   hl
    djnz  char_loop

    pop   de
    inc   e
    jr    string_loop

string_padding::
    ld    a, e
    and   #0x1f     ;; current column
    cp    #0x1f
    jr    nc, print_done

    ld    b, #8
    push  de
    xor   a
space_loop::
    ld    (de), a
    inc   d
    djnz  space_loop

    pop   de
    inc   e
    jr    string_padding

print_done::
    pop   ix
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

// only used while VRAM lines 2..20 are blacked out -> pick a location there
static uint8_t __at(0x4020) bitcounter;
static uint8_t __at(0x4021) pixel_buffer[6];

void
print_ip_addr(const ipv4_address_t *ip, uint8_t *p)
__naked
{
  (void) ip, p;

  __asm

    push  ix

    ld    ix, #4
    add   ix, sp

    xor   a, a
    ld    (_bitcounter), a

    ld    l, 2(ix)    ;; HI(p)
    ld    h, 3(ix)    ;; LO(p)

    ld    c, #0       ;; loop counter AND octet index
00001$:               ;; four octets
    ld    a, 0(ix)    ;; LO(ip)
    ld    d, 1(ix)    ;; HI(ip)
    add   a, c
    ld    e, a

    ld    a, (de)
    cp    a, #10
    jr    c, 00004$    ;; < 10? print only single digit
    cp    a, #100
    jr    c, 00002$    ;; no hundreds? skip entirely, not even a zero
    ld    d, #0
00003$:   ;; loop to count hundreds
    inc   d
    sub   a, #100
    cp    a, #100
    jr    nc, 00003$

    call  print_condensed_digit      ;; X__
    call  print_space

00002$:   ;; hundreds done
    cp    a, #10
    jr    c, 00004$
    ld    d, #0
00005$:   ;; loop to count tens
    inc   d
    sub   a, #10
    cp    a, #10
    jr    nc, 00005$

    call  print_condensed_digit      ;; _X_
    call  print_space

00004$:   ;; tens done
    ld    d, a
    call  print_condensed_digit      ;; __X

    ;; print period?
    inc   c
    ld    a, c
    cp    a, #4            ;; last octet? no period
    jr    z, 00006$
    call  print_period
    jr    00001$           ;; next octet

00006$:    ;; all octets done

    ;; shift in spaces so the last piece of the last
    ;; digit comes into place
    ld    a, (_bitcounter)
    ld    b, a
    ld    a, #8
    sub   a, b
    ld    b, a
00007$:
    call  print_space
    djnz  00007$

    pop   ix
    ret

;; ***********************************************************************
;; subroutine: print a period
;; HL=VRAM position (on entry and exit)
;; ***********************************************************************
print_period::
    push  bc
    ld    c, #4
    jr    print_period_continued

;; ***********************************************************************
;; subroutine: print a pixel-wide space
;; HL=VRAM position (on entry and exit)
;; ***********************************************************************
print_space::
    push  bc
    ld    c, #1
print_period_continued::
    push  af
00022$:          ;; column loop
    ld    b, #6
    push  hl
00023$:          ;; row loop
    sla   (hl)

    ;; a period should be drawn if
    ;; B (row) > 3 AND (C (row) == either 2 or 3)
    ld    a, b
    cp    #3
    jr    nc, 00024$
    bit   1, c
    jr    z, 00024$
    inc   (hl)
00024$:          ;; done printing period
    inc   h
    djnz  00023$
    pop   hl

    call flush_if_needed

    dec   c
    jr    nz,00022$
    pop   af
    pop   bc
    ret

;; ***********************************************************************
;; subroutine: print a condensed digit
;; HL=VRAM position (on entry and exit)
;; D=digit
;; ***********************************************************************
print_condensed_digit::
    push  af
    push  bc
    push  de
    ;; copy digit pixels to pixel_buffer

    push  hl
    ld    a, d
    ld    b, #6
    ld    de, #_pixel_buffer
    ld    hl, #_font_data + 16*8 + 1   ;; first pixel row of character '0'
    add   a, a
    add   a, a
    add   a, a
    add   a, l
    ld    l, a
00031$:
    ld    a, (hl)
    add   a, a
    ld    (de), a
    inc   hl
    inc   de
    djnz  00031$
    pop   hl

    ld    c, #5
00032$:               ;; five columns
    ld    b, #6
    ld    de, #_pixel_buffer
00033$:               ;; six rows
    ld    a, (de)
    rl    a
    ld    (de), a
    rl    (hl)

    ;; pixel column 3 is OR-ed with column 4

    ld    a, c
    cp    a, #3
    ld    a, (de)     ;; needed after JR NZ, keep Z flag intact
    jr    nz, 00034$  ;; compress pixel row #3?
    rl    a
    ld    (de), a
    jr    nc, 00034$    ;; any additional pixel?
    ld    a, (hl)
    or    #1
    ld    (hl), a

00034$:
    ;; next row of pixels
    inc   h
    inc   de
    djnz  00033$

    ld    a, h
    sub   a, #6
    ld    h, a    ;; restore HI(p)
    call flush_if_needed

    ;; next column of pixels
    dec   c
    jr    nz, 00032$

    pop   de
    pop   bc
    pop   af
    ret

;; ***********************************************************************
;; subroutine: note that VRAM pixels have now shifted one bit,
;;             possibly increase HL
;; HL=VRAM position (on entry and exit)
;; destroys AF
;; ***********************************************************************
flush_if_needed::
    ld    a, (_bitcounter)
    inc   a
    cp    a, #8
    jr    nz, flush_not_needed
    inc   hl
    xor   a
flush_not_needed::
    ld    (_bitcounter), a
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

static key_t previous_key;    // initially == KEY_NONE (0) */
static bool first_repetition;

key_t
wait_key(void)
__naked
{
  __asm

    ;; ------------------------------------------------------------------------
    ;; is the previous key still being pressed?
    ;; ------------------------------------------------------------------------

    call _poll_key

    or   a, a
    jr   z, wait_key_no_repetition
    ld   a, (_previous_key)
    cp   a, b
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; yes, the previous key is still being pressed
    ;; see if it remains pressed until the repetition timer expires
    ;; ------------------------------------------------------------------------

wait_key_repetition_loop:

    call _poll_key
    ld   a, (_previous_key)
    cp   a, b
    jr   nz, wait_key_no_repetition

    ;; ------------------------------------------------------------------------
    ;; decide on a timeout, depending on whether this is the first repetition
    ;; ------------------------------------------------------------------------

    ld   hl, #_timer_tick_count + 1
    ld   a, (hl)     ;; high byte of ticks
    or   a, a        ;; non-zero? then definitely timeout
    jr   nz, wait_key_repeat
    ld   a, (_first_repetition)
    or   a, a
    ld   a, #REPEAT_FIRST_TIMEOUT
    jr   nz, wait_key_check_repetition
    ld   a, #REPEAT_NEXT_TIMEOUT
wait_key_check_repetition:
    dec  hl          ;; now points to low byte of ticks
    cp   a, (hl)
    jr   nc, wait_key_repetition_loop

    ;; ------------------------------------------------------------------------
    ;; we have a repeat event, and this is no longer the first repetition
    ;; ------------------------------------------------------------------------

wait_key_repeat:
    xor  a, a              ;; value for _first_repetition
    jr   wait_key_finish

    ;; ------------------------------------------------------------------------
    ;; no repetition: instead wait for a key to become pressed
    ;; ------------------------------------------------------------------------

wait_key_no_repetition:

    call _poll_key
    or   a, a
    jr   z, wait_key_no_repetition

    ld   (_previous_key), a

    ;; ------------------------------------------------------------------------
    ;; Any repetition after this will be the first one, so A needs to be set
    ;; to a non-zero value. And it is: it is the non-zero key value.
    ;; ------------------------------------------------------------------------

wait_key_finish:
    ;; assume A holds value for _first_repetition, and B holds result
    ld   hl, #0
    ld   (_timer_tick_count), hl
    ld   (_first_repetition), a
    ld   l, b      ;; _poll_key returned same value in A and B
    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
set_attrs_impl(uint8_t attrs, uint8_t *attr_address, int len)
__naked
{
  (void) attrs, attr_address, len;

  __asm

    pop   de
    dec   sp
    pop   af
    pop   hl
    pop   bc
    push  bc
    push  hl
    dec   sp
    push  de

    ld    e, l
    ld    d, h
    inc   de
    ld    (hl), a
    ldir

    ret

  __endasm;
}

/* ------------------------------------------------------------------------- */

void
init_progress_display(void)
__naked
{
  __asm

    ld    hl, #0x5800
    ld    de, #0x5801
    ld    bc, #0x2e0
    xor   a
    ld    (hl), a
    ldir

    ld    c, #0x1f
    ld    a, #(PAPER(BLUE) | INK(BLUE))
    ld    (hl), a
    ldir

    ld    l, #14
    xor   a
    call  _show_attr_digit

    ld    l, #25
    ld    de, #_font_data + 8 * (75-32) + 1 ;; address of 'K' bits
    push  bc   ;; because the routine jumped to will pop that before ret:ing
    jp    show_attr_digit_address_known

  __endasm;
}
