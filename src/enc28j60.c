/*
 * Module enc28j60:
 *
 * Basic access to control registers and on-chip memory of the
 * Microchip ENC28J60 Ethernet host.
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.org>
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

#include "enc28j60.h"
#include "spectrum.h"
#include "speccyboot.h"
#include "logging.h"

/* ------------------------------------------------------------------------- */

#define ECON1_RXEN    (4)

/* ------------------------------------------------------------------------- */

/*
 * Useful macro for avoiding warnings about unused arguments
 */
#define ARG_NOT_USED(a)  ((void) a)

/* ------------------------------------------------------------------------- */

/*
 * Opcodes for SPI commands
 */
#define SPI_OPCODE_RCR(reg)     (0x00 | (reg))
#define SPI_OPCODE_WCR(reg)     (0x40 | (reg))

/* ------------------------------------------------------------------------- */

static void
SPI_DELAY(void)
{
  /*
   * Delay loop (> 1ms @3.55MHz)
   */
  __asm
  ld  b, #0
lp:
  nop
  djnz  lp
  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * FRAM configuration: FRAM enabled, bank 0 selected
 */
#define FRAM_CONFIG   (0)

#define SPI_PORT      (159)

/* ------------------------------------------------------------------------- */

/*
 * Write 8 bits, read 8 bits response
 *
 * Assembly code assumes argument to be passed in (sp + 4),
 * returns result in l
 */
static uint8_t
write8read8(uint8_t byte_to_write)
__naked
{
  (void) byte_to_write;     /* silence warning about not being used */

  __asm
  
    ld  hl, #2
    add hl, sp
    ld  d, (hl)

    ; each half-cycle is 32 T-states => approx 54.6kbps bit-rate @3.5MHz
  
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    ; WRITE BYTE: d holds the value to write
    ; MOSI is written directly after SCK has been pulled low
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
    ld  l, #0x80    ; 7     x
    ld  c, #159     ; 7			x
    ld  a, l        ; 4			x   a is now 0x80
    rl  d           ; 8			x
    rra             ; 4			x   a is now (0x40 | d.bit7 * 0x80)
    out (c), a      ; 12		x   CS=0, SCK=0, MOSI=d.bit7
  
    ; shift out bit 7

    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
  
    ; shift out bit 6

    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit6 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit6
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
  
    ; shift out bit 5
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit5 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit5
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 4
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit4 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit4
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 3
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit3 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit3
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 2
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit2 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit2
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 1
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit1 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit1
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 0
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit0 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit0
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
  
    ; keep high for 32 T-states
    ld hl, #0x4140  ; 10    H
    ld hl, #0x4140  ; 10    H   delay
    out (c), l      ; 12		H
  
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    ; READ BYTE: d holds the value read
    ; MISO is read just before SCK is pulled high
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
    nop             ; 4     L   delay
    nop             ; 4     L   delay
  
    ; shift in bit 7
  
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 6
    
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 5
    
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 4
    
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 3

    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 2
    
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
  
    ; shift in bit 1
     
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L
    
    ; shift in bit 0
    
    in  e, (c)      ; 12		L
    out	(c), h      ; 12		L
    rr e            ; 8			H
    rr e            ; 8			H
    nop             ; 4     H   delay
    out (c), l      ; 12		H
    rl d            ; 8			L

    ld l, d         ; 4     L
    ld a, #0x48     ; 7     L
    nop             ; 4     L   make sure we wait > 1 half-cycle before CS
    out (c), a      ; 12    L   release CS
  
    ret
  
  __endasm;
}

/* ------------------------------------------------------------------------- */

/*
 * Write 2x8 bits
 *
 * Assembly code assumes cmd to be passed in (sp + 2), data in (sp + 3)
 */
static void
write2x8(uint8_t cmd, uint8_t data)
__naked
{
  (void) cmd;     /* silence warning about not being used */
  (void) data;    /* silence warning about not being used */
  
  __asm
  
    ld  hl, #2
    add hl, sp
    ld  d, (hl)     ;           cmd
    inc hl
    ld  e, (hl)     ;           data
    
    ; each half-cycle is 32 T-states => approx 54.6kbps bit-rate @3.5MHz
    
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    ; WRITE BYTE: d holds the value to write
    ; MOSI is written directly after SCK has been pulled low
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    
    ld  l, #0x80
    ld  c, #159
    ld  a, l        ; 4			x   a is now 0x80
    rl  d
    rra             ; 4			x   a is now (0x40 | d.bit7 * 0x80)
    out (c), a      ; 12		x   CS=0, SCK=0, MOSI=d.bit7
    
    ; shift out bit 7
    
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 6
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit6 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit6
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 5
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit5 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit5
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 4
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit4 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit4
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 3
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit3 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit3
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 2
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit2 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit2
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 1
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit1 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit1
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 0
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  d           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit0 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit0
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    ; WRITE BYTE: e holds the value to write
    ; MOSI is written directly after SCK has been pulled low
    ; - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    
    ; shift out bit 7
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit6 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit6
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 6
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit6 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit6
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 5
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit5 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit5
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 4
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit4 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit4
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 3
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit3 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit3
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 2
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit2 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit2
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 1
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit1 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit1
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; shift out bit 0
    
    ld  a, l        ; 4			H   a is now 0x80
    rl  e           ; 8			H
    rra             ; 4			H   a is now (0x40 | d.bit0 * 0x80)
    nop             ; 4     H   delay
    out (c), a      ; 12		H   CS=0, SCK=0, MOSI=d.bit0
    inc a           ; 4     L   SCK := 1
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    nop             ; 4     L   delay
    out (c), a      ; 12		L
    
    ; keep high for 32 T-states
    dec a           ; 4     L   SCK := 0
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    out (c), a      ; 12		H
    
    ; release CS after > 32 low T-states
    ld  a, #0x48    ; 7     L   CS := 1
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    nop             ; 4     H   delay
    out (c), a      ; 12		H
    
    ret
    
  __endasm;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_init(void)
{
  Z80_PORT_WRITE(sbt_cfg_port, FRAM_CONFIG);                /* SPI_RST -> 0 */
  SPI_DELAY();
  Z80_PORT_WRITE(sbt_cfg_port, FRAM_CONFIG | SPI_RST);      /* SPI_RST -> 1 */
  SPI_DELAY();
}

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_read_register(bool    is_mac_or_mii,
                       uint8_t bank,
                       uint8_t reg)
{
  uint8_t result;
 
  logging_add_entry("read \200:\200", (uint8_t) bank, (uint8_t) reg);
  
  if (reg < REGISTERS_IN_ALL_BANKS) {
    write2x8(SPI_OPCODE_WCR(ECON1), bank | ECON1_RXEN);  /* select bank */
  }
  
#if 0
  spi_active();

  spi_write(SPI_OPCODE_RCR(reg));
  if (is_mac_or_mii) {
    (void) spi_read();        /* dummy byte -- ignored */
  }
  result = spi_read();

  spi_idle();
#else
  result = write8read8(SPI_OPCODE_RCR(reg));
#endif
  
  return result;
}

/* ------------------------------------------------------------------------- */

void
enc28j60_write_register(bool    is_mac_or_mii,
                        uint8_t bank,
                        uint8_t reg,
                        uint8_t value)
{
  ARG_NOT_USED(is_mac_or_mii);
  
  logging_add_entry("write \200:\200:=\200", (uint8_t) bank, (uint8_t) reg, (uint8_t) value);

  if (reg < REGISTERS_IN_ALL_BANKS) {
    write2x8(SPI_OPCODE_WCR(ECON1), bank | ECON1_RXEN);  /* select bank */
  }
  
#if 0
  spi_active();

  spi_write(SPI_OPCODE_WCR(reg));
  spi_write(value);
  spi_idle();
#else
  write2x8(SPI_OPCODE_WCR(reg), value);
#endif
}

/* ------------------------------------------------------------------------- */

uint8_t
enc28j60_poll(void)
{
  uint8_t result = write8read8(26);
  Z80_PORT_WRITE(sbt_cfg_port, result);                /* SPI_RST -> 0 */
  return 0; /* FIXME */
}
