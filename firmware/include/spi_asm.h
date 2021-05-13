/*
 * Module spi_asm:
 *
 * Definitions for bit-banged SPI access, common to C and assembly files.
 * Must only contain basic preprocessor instructions, no C declarations.
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2012-  Patrik Persson & Imrich Kolkol
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

#ifndef SPECCYBOOT_SPI_ASM_INCLUSION_GUARD
#define SPECCYBOOT_SPI_ASM_INCLUSION_GUARD

/* ------------------------------------------------------------------------- */

/*
 * Check that at least one valid HWTARGET_XXX flag is set
 */
#ifndef HWTARGET_SPECCYBOOT
#ifndef HWTARGET_DGBOOT
#error Invalid HWTARGET!
#endif
#endif

/* ------------------------------------------------------------------------- */

/*
 * Port for SPI communication
 */

#ifdef HWTARGET_SPECCYBOOT

/* Default SpeccyBoot configuration */

#  define SPI_OUT                         0x9f
#  define SPI_IN                          SPI_OUT

#  define SPI_CS                          0x08

#endif

#ifdef HWTARGET_DGBOOT

/* Imrich Kolkol's DGBoot maps SPI IN and OUT to different registers */

#  define SPI_OUT                         0x3f
#  define SPI_IN                          0x1f

#  define SPI_CS                          0x02

#endif

/* ------------------------------------------------------------------------- */

/* FIXME: these are better defined as variables in spi.inc */

#define SPI_SCK                         0x01
#define SPI_RST                         0x40
#define SPI_MOSI                        0x80

#define PAGE_OUT                        0x20

#define SPI_IDLE                        SPI_RST

#endif /* SPECCYBOOT_SPI_ASM_INCLUSION_GUARD */
