/*
 * Module spi:
 *
 * Bit-banged SPI access.
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

#ifndef SPECCYBOOT_SPI_INCLUSION_GUARD
#define SPECCYBOOT_SPI_INCLUSION_GUARD

#include <stdint.h>

/* ------------------------------------------------------------------------- */

/*
 * Port for SPI communication
 */
#define SPI_PORT                        (0x9f)

/* ------------------------------------------------------------------------- */

/*
 * Masks for the individual bits
 */
#define SPI_SCK                         (0x01)
#define SPI_CS                          (0x08)
#define SPI_RST                         (0x40)
#define SPI_MOSI                        (0x80)

/*
 * SPI idle, MOSI=0, RST high, CS low, SCK low
 */
#define SPI_IDLE                        SPI_RST

/* ------------------------------------------------------------------------- */

/*
 * Note the funny notation for 'rl x' as 'rl a, x' below:
 *
 * Apparently as-z80 got the 'rl x' instruction mixed up into the same group
 * as 'add x', 'cp x', and so on, and somehow thinks 'a' is an implicit
 * operand. We need to give the full version of the instruction for as-z80 to
 * figure things out in macros, because these will expand to a single line,
 * which takes some intelligence to parse correctly -- hence the use of
 * 'rl a, x' below.
 *
 * I got the idea of 'rl a, x' from here:
 * http://shop-pdp.kent.edu/ashtml/asz80.htm
 */

#pragma save
#pragma sdcc_hash + 

/*
 * Reset the device on SPI (technically RST is not an SPI pin, but it happens
 * to fit here nicely).
 *
 * Data sheet, Table 16.3: Trstlow = 400ns
 * (minimal RST low time, shorter pulses are filtered out)
 *
 * 400ns < 2 T-states == 571ns    (no problem at all)
 * 
 * Data sheet, #11.2:
 *
 * Wait at least 50us after a System Reset before accessing PHY registers.
 * Perform an explicit delay here to be absolutely sure.
 *
 * 64 iterations, each is 18 T-states, 64x18 = 1152 T-states > 300us @3.55MHz
 *
 * Use A rather than B register to avoid confusing register allocation.
 */
#define SPI_RESET                         \
  xor a, a                                \
  out (SPI_PORT), a                       \
  ld  a, #SPI_IDLE                        \
  out (SPI_PORT), a                       \
                                          \
spi_reset_loop:                           \
  dec   a                                 \
  jr    nz,  spi_reset_loop

/*
 * Write bit 7 from register REG to SPI MOSI, and then shift REG left one bit
 */
#define SPI_WRITE_BIT_FROM(REG)           \
  ld    a, #SPI_IDLE+SPI_IDLE             \
  rl    a, REG                            \
  rra                                     \
  out   (SPI_PORT), a                     \
  inc   a                                 \
  out   (SPI_PORT), a

/*
 * Shift register REG left one bit, and shift in a bit from SPI MISO to bit 0
 */
#define SPI_READ_BIT_TO(REG)              \
  ld    a, #SPI_IDLE                      \
  out   (SPI_PORT), a                     \
  inc   a                                 \
  out   (SPI_PORT), a                     \
  in    a, (SPI_PORT)                     \
  rra                                     \
  rl    a, REG

/*
 * Read one bit to accumulator, requires C=0x9f, H=0x40. Destroys F, L.
 * Takes 12 + 12 + 8 + 4 + 4 + 12 + 4
 *   = 56 T-states
 */
#define SPI_READ_BIT_TO_ACC               \
  out   (c), h                            \
  inc   h                                 \
  out   (c), h                            \
  dec   h                                 \
  in    l, (c)                            \
  rr    a, l                              \
  rla

/*
 * End an SPI transaction by pulling SCK low, then CS high.
 */
#define SPI_END_TRANSACTION               \
  ld  a, #SPI_IDLE                        \
  out (SPI_PORT), a                       \
  ld  a, #SPI_IDLE+SPI_CS                 \
  out (SPI_PORT), a

#pragma restore

/* ========================================================================= */

/*
 * Macros for beginning and ending an SPI transaction. The braces are there
 * for checking that two matching calls are used.
 */
#define spi_start_transaction(opcode)   spi_write_byte(opcode); {

/*
 * End an SPI transaction by pulling SCK low, then CS high.
 */
#define spi_end_transaction()     }   \
  __asm                               \
  SPI_END_TRANSACTION                 \
  __endasm

/* ------------------------------------------------------------------------- */

/*
 * Read 8 bits from SPI.
 */
uint8_t
spi_read_byte(void)
__naked;

/* ------------------------------------------------------------------------- */

/*
 * Write 8 bits to SPI.
 */
void
spi_write_byte(uint8_t x)
__naked;

#endif /* SPECCYBOOT_SPI_INCLUSION_GUARD */
