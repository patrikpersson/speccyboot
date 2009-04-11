/*
 * Module spectrum:
 *
 * Access to ZX Spectrum features (screen, keyboard, joystick, ...)
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>

#include "spectrum.h"

/* ------------------------------------------------------------------------- */
/* Screen memory                                                             */
/* ------------------------------------------------------------------------- */

#define BITMAP_BASE   (0x4000)
#define BITMAP_SIZE   (0x1800)
#define ATTRS_BASE    ((BITMAP_BASE) + (BITMAP_SIZE))
#define ATTRS_SIZE    (0x300)

static uint8_t * bitmap = (uint8_t *) (BITMAP_BASE);
static uint8_t * attrs  = (uint8_t *) (ATTRS_BASE);

/* ------------------------------------------------------------------------- */

static Z80_PORT(0x7ffe) space_row_port;
static Z80_PORT(0xbffe) enter_row_port;
static Z80_PORT(0xeffe) digits_0_to_6_row_port;

static Z80_PORT(0x001f) joystick_port;            /* kempston joystick */

#define KEY_SPACE     (0x01)
#define KEY_ENTER     (0x01)
#define KEY_0         (0x01)
#define KEY_6         (0x10)
#define KEY_7         (0x08)

#define JOY_FIRE      (0x10)
#define JOY_UP        (0x08)
#define JOY_DOWN      (0x04)

#define KEY_PRESSED(bits, mask)    (((bits) & (mask)) != (mask))
#define JOY_PRESSED(bits, mask)    (((bits) & (mask)) != 0)

static Z80_PORT(0x00fe) ula_port;     /* for border color */

/* ------------------------------------------------------------------------- */

static uint8_t copy_font_code[] = {
  0xF3,               /* di */
  0x3E, 0x20,         /* ld a, #0x20 */
  0xD3, 0x9F,         /* out (0x9f), a    -- page out FRAM */
  0x01, 0xFD, 0x7F,   /* ld bc, #0x7FFD */
  0x3E, 0x10,         /* ld a, #0x10      -- page in 128k ROM 1 (48k BASIC) */
  0xED, 0x79,         /* out (c), a */
  0x21, 0x00, 0x3D,   /* ld hl, #0x3D00 */
  0x11, 0x00, 0x00,   /* ld de, #0xXXYY */
  0x01, 0x00, 0x03,   /* ld bc, #0x0300 */
  0xED, 0xB0,         /* ldir */
  0xAF,               /* xor a */
  0xD3, 0x9F,         /* out (0x9f), a */
  0x01, 0xFD, 0x7F,   /* ld bc, #0x7FFD */
  0xED, 0x79,         /* out (c), a */
  0xFB,               /* ei */
  0xC9                /* ret */
};

#define OFFSET_OF_FONT_ADDR_LSB     (16)
#define OFFSET_OF_FONT_ADDR_MSB     (17)

#define NBR_OF_CHARS                (96)      /* 96 chars (32..127) */
#define BYTES_PER_CHAR              (8)

static uint8_t fontdata[NBR_OF_CHARS * BYTES_PER_CHAR];

/* ------------------------------------------------------------------------- */

/*
 * Simple hex conversion
 */
#define hexdigit(n) ( ((n) < 10) ? ((n) + '0') : ((n) + 'a' - 10))

/* -------------------------------------------------------------------------
 * Character attributes for print_char_at()
 * ------------------------------------------------------------------------- */
#define ATTR_NORMAL     (0x00)
#define ATTR_BOLD       (0x01)

/* ------------------------------------------------------------------------- */

static void
print_char_at(uint8_t ch, uint8_t *dst_p, bool is_bold)
{
  uint8_t *src_p = &fontdata[(ch - ' ') << 3];
  uint16_t i;
  for (i = 0; i < 0x0800; i += 0x0100) {
    uint8_t src_pixels = *src_p++;
    dst_p[i] = (is_bold)  ? (src_pixels | (src_pixels >> 1)) : (src_pixels);
  }
}

/* ------------------------------------------------------------------------- */

void
spectrum_init_font(void)
{
  copy_font_code[OFFSET_OF_FONT_ADDR_LSB] = (((uint16_t) &fontdata) & 0xff);
  copy_font_code[OFFSET_OF_FONT_ADDR_MSB] = ((((uint16_t) &fontdata) >> 8) & 0xff);
  JUMP_TO(copy_font_code);
}

/* ------------------------------------------------------------------------- */

void
spectrum_cls(const uint8_t screen_attrs, const uint8_t border_attrs)
{
  uint16_t i;

  uint8_t *p = (uint8_t *) BITMAP_BASE;
  for (i = 0; i < BITMAP_SIZE; i++) {
    *p++ = 0;
  }
  
  spectrum_set_attrs(screen_attrs, 0, 0, ATTRS_SIZE);
  
  Z80_PORT_WRITE(ula_port, border_attrs);
}

/* ------------------------------------------------------------------------- */

void
spectrum_set_attrs(const uint8_t screen_attrs,
                   const uint8_t row,
                   const uint8_t col,
                   const uint16_t n)
{
  uint16_t i;
  uint8_t *p = ((uint8_t *) ATTRS_BASE) + col + (row * ROW_LENGTH);
  for (i = 0; i < n; i++) {
    *p++ = screen_attrs;
  }
}

/* ------------------------------------------------------------------------- */

void
spectrum_print_at(uint8_t row, uint8_t col,
                  const char *str, const uint8_t *args)
{
  uint8_t *dst_p = ((uint8_t *) BITMAP_BASE)
                 + ((row & 0x18) << 8) + ((row & 0x07) << 5) + col;
  uint8_t attrs = ATTR_NORMAL;
  
  while (col < ROW_LENGTH && *str) {
    uint8_t ch = *str++;
    
    switch (ch) {
      case BOLD_ON_CHAR:
        attrs |= ATTR_BOLD;
        continue;
      case BOLD_OFF_CHAR:
        attrs &= ~ATTR_BOLD;
        continue;
      case DEC_ARG_CHAR:
      {
        uint8_t arg = *args++;
        if (arg >= 100) {
          print_char_at('0' + (arg / 100), dst_p++, attrs);
          col++;
          arg %= 100;
        }
        if (arg >= 10 && col < ROW_LENGTH) {
          print_char_at('0' + (arg / 10), dst_p++, attrs);
          col++;
          arg %= 10;
        }
        if (col < ROW_LENGTH) {
          print_char_at('0' + arg, dst_p++, attrs);
          col++;
        }
        break;
      }
      case HEX_ARG_CHAR:
      {
        uint8_t arg = *args++;
        print_char_at(hexdigit(arg >> 4), dst_p++, attrs);
        col++;
        if (col < ROW_LENGTH) {
          print_char_at(hexdigit(arg & 0x0f), dst_p++, attrs);
          col ++;
        }
        break;
      }
      default:
        print_char_at(ch, dst_p++, attrs);
        col ++;
        break;
    }
    
  }
}

/* ------------------------------------------------------------------------- */

void
spectrum_scroll(void)
{
  _asm
  ld   hl, #(BITMAP_BASE + 0x0020)
  ld   de, #BITMAP_BASE
  ld   bc, #0x07e0
  ldir
  
  ld   a,  #0x40
lp1:
  ld   h,  a  
  set  3,  h
  ld   l,  #0
  ; hl is 0x4n00 where n is 8..f
  ld   d,  a
  ld   e,  #0xe0
  ; de is 0x4ne0 where n is 0..7
  ld   bc, #0x0020
  ldir
  inc  a
  cp   #0x48
  jr   nz,lp1
  
  ld   hl, #(BITMAP_BASE + 0x0820)
  ld   de, #(BITMAP_BASE + 0x0800)
  ld   bc, #0x07e0
  ldir
  
lp2:
  ld   h,  a  
  set  #4,  h
  res  #3,  h
  ld   l,  #0
  ; hl is 0x5n00 where n is 0..8
  ld   d,  a
  ld   e,  #0xe0
  ; de is 0x4ne0 where n is 8..f
  ld   bc, #0x0020
  ldir
  inc  a
  cp   #0x50
  jr   nz,lp2
  
  ld   hl, #(BITMAP_BASE + 0x1020)
  ld   de, #(BITMAP_BASE + 0x1000)
  ld   bc, #0x07e0
  ldir
  
  ld   e,    #0
  ld   h,    #0x50
lp4:
  ld   b,    #32
  ld   l,    #0xe0
lp3:
  ld   (hl), e
  inc  hl
  djnz lp3
  ld   a, h
  cp   #0x58
  jr   nz, lp4
  
  _endasm;
}

/* ------------------------------------------------------------------------- */

enum spectrum_input_t
spectrum_poll_input(void)
{
  uint8_t dig_state = Z80_PORT_READ(digits_0_to_6_row_port);
  uint8_t spc_state = Z80_PORT_READ(space_row_port);
  uint8_t ent_state = Z80_PORT_READ(enter_row_port);
  uint8_t joy_state = Z80_PORT_READ(joystick_port);
  
  if (KEY_PRESSED(dig_state, KEY_0)
      || KEY_PRESSED(spc_state, KEY_SPACE)
      || KEY_PRESSED(ent_state, KEY_ENTER)
      || JOY_PRESSED(joy_state, JOY_FIRE))
  {
    return INPUT_FIRE;
  }
  if (KEY_PRESSED(dig_state, KEY_6) || JOY_PRESSED(joy_state, JOY_DOWN)) {
    return INPUT_DOWN;
  }
  if (KEY_PRESSED(dig_state, KEY_7) || JOY_PRESSED(joy_state, JOY_UP)) {
    return INPUT_UP;
  }
  
  return INPUT_NONE;
}

/* ------------------------------------------------------------------------- */

enum spectrum_input_t
spectrum_wait_input(void)
{
  enum spectrum_input_t current_input;
  
  /* Spin until any previous key is released */
  while (spectrum_poll_input() != INPUT_NONE)
    ;

  /* Spin until a key is pressed */
  do {
    current_input = spectrum_poll_input();
  } while (current_input == INPUT_NONE);

  return current_input;
}
