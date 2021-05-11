/*
 * Module globals:
 *
 * Shared state (buffer for received frame, system mode)
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

#include "globals.h"

/* ========================================================================= */

union rx_frame_t  rx_frame;         /* buffer for received packet data */

/* =========================================================================
 * stuff for snapshot loading
 * ========================================================================= */

uint8_t snapshot_header[Z80_HEADER_RESIDENT_SIZE];

/*
 * For progress display while loading a snapshot.
 *
 * For 128k snapshots, 'kilobytes_expected' is set in s_header (z80_loader.c)
 */

uint8_t kilobytes_loaded   = 0;
uint8_t kilobytes_expected = 48;

/* =========================================================================
 * Ethernet/IP/UDP stuff
 * ========================================================================= */

/* received Ethernet header, along with some administrative information */
struct eth_adm_t rx_eth_adm;

struct ip_config_t ip_config;

/* header template for outgoing UDP packets */
uint8_t header_template[IPV4_HEADER_SIZE + UDP_HEADER_SIZE];

uint16_t tftp_client_port = htons(0xc000); /* client-side UDP port for TFTP */

uint16_t ip_checksum;

/* =========================================================================
 * TFTP stuff
 * ========================================================================= */

uint8_t *tftp_write_pos
  = (uint8_t *)
#ifdef STAGE2_IN_RAM
&stage2;
#else
&snapshot_list;
#endif

/* -------------------------------------------------------------------------
 * If non-NULL, this function is called for every received TFTP packet
 * (instead of regular raw data file handling)
 * ------------------------------------------------------------------------- */
void (*tftp_receive_hook)(void);
