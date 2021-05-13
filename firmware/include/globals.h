/*
 * Module globals:
 *
 * Shared state (buffer for received frame, system mode)
 *
 * Part of SpeccyBoot <https://github.com/patrikpersson/speccyboot>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009- Patrik Persson
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

#include "eth.h"
#include "udp_ip.h"
#include "tftp.h"
#include "context_switch.h"

#ifndef SPECCYBOOT_GLOBALS_INCLUSION_GUARD
#define SPECCYBOOT_GLOBALS_INCLUSION_GUARD

/* ------------------------------------------------------------------------- */

/* Maximal number of snapshots we can list */
#define MAX_SNAPSHOTS    (400)

/* ========================================================================= */

/*
 * See context_switch.h about these addresses
 */

/* ------------------------------------------------------------------------ */

/* Important addresses, defined in crt0.asm */
extern uint8_t stage2;        /* Beginning of second-stage loader */
extern uint8_t snapshot_list;  /* First byte after second-stage loader */

/* ========================================================================
 * absolute address data:
 * 0x5b00   stack        0x60 bytes
 * 0x5b60   digit data   0x3C (decimal 60) bytes
 * 0xFD03   font data    0x2FD bytes
 * ======================================================================== */

/* Stack address (defined here so crt0.asm can find it via linker) */
#define STACK_SIZE   (0x60)
uint8_t __at(0x5b00 + STACK_SIZE) stack_top;

/*
 * Font data for digits 0..9. Six bytes per digit (as first scanline is
 * always zero and need not be stored). Copied from font_data.
 * Placed on an absolute address so we can be sure that it's all in a single
 * 256b page. (Makes addressing simpler.)
 */
#define DIGIT_DATA_ADDR      (0x5b00 + STACK_SIZE)
extern uint8_t  __at(DIGIT_DATA_ADDR) digit_data[60 * 10];

/*
 * Buffer for font data (copied from 48K ROM by crt0.asm)
 *
 * The 3 is there for a reason: it allows us to quickly load BC with a sensible
 * value while copying data from Spectrum ROM to this buffer, in crt0.asm.
 * The '3' also means that the last 3 bytes of the last character (127,
 * copyright sign) will be garbled (mapped to ROM). Shouldn't matter, as this
 * character (DEL) is not expected in UNIX filenames. */
uint8_t __at(0xfd03) font_data[0x300];

extern uint8_t *tftp_write_pos;   /* position to write received TFTP data to */

/* IP address configuration */
extern struct ip_config_t {
 ipv4_address_t host_address;
 ipv4_address_t tftp_server_address;
} ip_config;

/* ------------------------------------------------------------------------- */

/* Buffer for received packet data */
extern union rx_frame_t {
  /* -------------------------------------------------------------- Raw data */
  uint8_t raw_bytes[IPV4_HEADER_SIZE + UDP_HEADER_SIZE + TFTP_HEADER_SIZE + TFTP_DATA_MAXSIZE];
  /* --------------------------------------------------- Snapshot name array */
  const char *snapshot_names [MAX_SNAPSHOTS];
} rx_frame;

#endif /* SPECCYBOOT_GLOBALS_INCLUSION_GUARD */
