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
#include "arp.h"
#include "bootp.h"
#include "dhcp.h"
#include "tftp.h"
#include "context_switch.h"

/* ------------------------------------------------------------------------- */

/* Maximal number of snapshots we can list */
#define MAX_SNAPSHOTS    (400)

/* ========================================================================= */

/*
 * See context_switch.h about these addresses
 */

/* Stack address (defined here so crt0.asm can find it via linker) */
uint8_t __at(0x5b60) stack_top;

/* Buffer for font data (copied from 48K ROM by crt0.asm) */
uint8_t __at(0x5F31) font_data[0x300];

/*
 * Storage for Z80 snapshot header. This is used while loading a snapshot,
 * so we re-use some of the space used for font data (SPACE to &)
 */
struct z80_snapshot_header_t  __at(0x5F31) snapshot_header;

/* ------------------------------------------------------------------------ */

/*
 * For progress display while loading a snapshot.
 *
 * For 128k snapshots, 'kilobytes_expected' is set in s_header (file_loader.c)
 */
extern uint8_t kilobytes_loaded;
extern uint8_t kilobytes_expected;

/* ------------------------------------------------------------------------ */

/* Buffer for snapshot list */
char __at(0x6300) snapshot_list_buf[0x9d00];

/* ========================================================================= */

/* Administrative Ethernet information, including Ethernet header */
extern struct eth_adm_t                 rx_eth_adm;

/* ------------------------------------------------------------------------- */

/*
 * This union is NOT designed for reading an entire Ethernet frame in one go:
 * this is not practical since, for example, the IP header has variable size.
 *
 * Instead, the purpose of this union is to preserve static memory by allowing
 * buffers to overlap whenever possible (unions). It also facilitates absolute
 * addressing of received data (making for efficient generated code).
 */
extern union rx_frame_t {
  /* --------------------------------------------------------- Raw IP header */
  struct ipv4_header_t                  ip;

  /* ------------------------------------------------------------------- UDP */
  PACKED_STRUCT() {
    struct ipv4_header_t                ip_header;
    struct udp_header_t                 header;

    union {
      struct bootp_packet_t             bootp;
      struct dhcp_packet_t              dhcp;
      PACKED_STRUCT() {
        struct tftp_header_t            header;
        union {
          uint8_t                       raw_bytes[TFTP_DATA_MAXSIZE];
          struct z80_snapshot_header_t  z80;
        } data;
      } tftp;
    } app;
  } udp;

  /* ------------------------------------------------------------------- ARP */
  struct arp_ip_ethernet_t              arp;

  /* --------------------------------------------------- Snapshot name array */
  const char *snapshot_names [MAX_SNAPSHOTS];
} rx_frame;
