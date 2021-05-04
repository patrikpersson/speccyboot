/*
 * Module util:
 *
 * Various low-level useful stuff.
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

#ifndef SPECCYBOOT_UTIL_INCLUSION_GUARD
#define SPECCYBOOT_UTIL_INCLUSION_GUARD

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * MAC address
 * ------------------------------------------------------------------------- */

#ifndef MAC_ADDR_0

/*
 * These definitions can be overridden by passing new ones from Makefile
 * using -DMAC_ADDR_x=y
 *
 * (you need to do this to have more than one SpeccyBoot on the same LAN)
 *
 * NOTE: this is an LAA (Locally Administered Address), as signified by
 * bit 1 in MAC_ADDR_0.
 */

#define MAC_ADDR_0    (0xba)
#define MAC_ADDR_1    (0xdb)
#define MAC_ADDR_2    (0xad)
#define MAC_ADDR_3    (0xc0)
#define MAC_ADDR_4    (0xff)
#define MAC_ADDR_5    (0xee)

#endif

/* ------------------------------------------------------------------------- */

/*
 * Byteswapping/masking helpers
 */
#define HIBYTE(x)       (((uint16_t) (x)) >> 8)
#define LOBYTE(x)       (((uint16_t) (x)) & 0x00ffu)

#define BYTESWAP16(x)   (LOBYTE(x) * 0x0100 + HIBYTE(x))
#define htons(n)        BYTESWAP16(n)
#define ntohs           htons

#define BITS0TO7(x)     LOBYTE(x)
#define BITS8TO15(x)    (((x) >> 8) & 0x00ffu)
#define BITS16TO23(x)   (((x) >> 16) & 0x00ffu)
#define BITS24TO31(x)   (((x) >> 24) & 0x00ffu)

#define BYTESWAP32(x)   (  BITS0TO7(x)   * 0x01000000u                        \
                         + BITS8TO15(x)  * 0x00010000u                        \
                         + BITS16TO23(x) * 0x00000100u                        \
                         + BITS24TO31(x) )

#define htonl(n)        BYTESWAP32(n)
#define ntohl           htonl

/* ------------------------------------------------------------------------- */

/*
 * Stringification, see e.g.,
 * https://gcc.gnu.org/onlinedocs/gcc-4.8.5/cpp/Stringification.html
 */

#define str(s) str2(s)
#define str2(s) #s

/* ------------------------------------------------------------------------- */

/* Packed structs */

#ifdef __SDCC
/* SDCC packs structs by default */
#define PACKED_STRUCT(name)  struct name
#else
#error Need to configure packed structs for compiler!
#endif

/* ------------------------------------------------------------------------- */

/* Compile-time asserts. No code generated. */
#define COMPILE_ASSERT(expr)                                                  \
  typedef int COMPILE_ASSERT_NAME(__LINE__) [expr ? 1 : -1]

#define COMPILE_ASSERT_NAME(ln)    COMPILE_ASSERT_NAME2(ln)
#define COMPILE_ASSERT_NAME2(ln)   assertion_at_line_ ## ln

/* ------------------------------------------------------------------------- */

/*
 * Default RAM bank (for a 16k/48k snapshot). Has to be even (non-contended)
 */
#define DEFAULT_BANK              (0)

/* ------------------------------------------------------------------------- */

#define TICKS_PER_SECOND          (50)

/* ------------------------------------------------------------------------- */

/* Type of a timer value */
typedef uint16_t timer_t;

/* -------------------------------------------------------------------------
 * Configure memory (128k and +2A/+3 registers)
 * ------------------------------------------------------------------------- */
#define memcfg(_c)              _memcfg_reg = (_c)

/* Spectrum 128k/+2 memory configuration register */
#define MEMCFG_ADDR             0x7ffd
#define MEMCFG_ROM_LO           0x10
#define MEMCFG_LOCK             0x20

__sfr __banked __at(MEMCFG_ADDR)      _memcfg_reg;

/* -------------------------------------------------------------------------
 * I/O addresses for sound registers (128k machines)
 * ------------------------------------------------------------------------- */
#define SND_REG_SELECT          0xfffd
#define SND_REG_VALUE           0xbffd

/* -------------------------------------------------------------------------
 * Reset/initialize the timer (set it to zero)
 * ------------------------------------------------------------------------- */
#define timer_reset()             timer_tick_count = 0
/* -------------------------------------------------------------------------
 * Returns the value of the timer, in ticks since it was reset
 * ------------------------------------------------------------------------- */
#define timer_value()             timer_tick_count

extern volatile timer_t timer_tick_count;

/* -------------------------------------------------------------------------
 * Compare memory blocks pointed to by HL and DE. Register B indicates the
 * number of bytes to compare (so, max 256).
 *
 * On return, Z is set if blocks are equal.
 * On a successful compare, B is zero, and HL and DE point to the
 * first byte after the compared blocks.
 * A is destroyed.
 * ------------------------------------------------------------------------- */
void
memory_compare(void);

/* -------------------------------------------------------------------------
 * Fill stack with magic value (for debugging/tuning)
 * ------------------------------------------------------------------------- */
#ifdef PAINT_STACK
#define STACK_MAGIC   (0x48)
void
paint_stack(void);
#endif

#endif /* SPECCYBOOT_UTIL_INCLUSION_GUARD */
