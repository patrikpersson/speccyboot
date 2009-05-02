/*
 * Module platform:
 *
 * Spectrum-specific and SDCC-specific platform details
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

#ifndef SPECCYBOOT_PLATFORM_INCLUSION_GUARD
#define SPECCYBOOT_PLATFORM_INCLUSION_GUARD

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
#define HIBYTE(x)       ((x & 0xff00u) ? ((x) >> 8) : 0)
#define LOBYTE(x)       ((x) & 0x00ffu)

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
 * Packed structs
 */

#ifdef SDCC
/* SDCC packs structs by default */
#define PACKED_STRUCT(name)  struct name
#else
#error Need to configure packed structs for compiler!
#endif

/* ------------------------------------------------------------------------- */

/*
 * Take a pointer, assume it points to a function, and call it.
 *
 * This thing is actually more efficient than inline assembly --
 * the compiler knows about the jump, so if JUMP_TO is used at the end of a
 * function, it results in slightly more efficient code (JP instead of
 * CALL + RET).
 *
 * More importantly, it saves a bit of stack.
 */
#define JUMP_TO(ptr) ((void (*)(void)) (ptr))()

/* -------------------------------------------------------------------------
 * Macros for Z80 I/O ports
 * ------------------------------------------------------------------------- */

#define Z80_PORT(addr)                   sfr banked at (addr)
#define Z80_PORT_WRITE(port, value)      port = value
#define Z80_PORT_READ(port)              (port)

#endif /* SPECCYBOOT_PLATFORM_INCLUSION_GUARD */

