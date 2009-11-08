/*
 * Module main:
 *
 * Loads a file over TFTP upon user request.
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

#include <stddef.h>

#include "util.h"
#include "z80_parser.h"

#include "dhcp.h"
#include "tftp.h"
#include "syslog.h"
#include "rxbuffer.h"

/* ------------------------------------------------------------------------- */

#define IMAGE_LIST_FILE                 "snapshots.lst"
#define DEFAULT_SNAPSHOT_FILE           "menu.z80"

/* ------------------------------------------------------------------------- */

#ifndef VERSION
#error Bad Makefile, does not define VERSION
#endif

/* ------------------------------------------------------------------------- */

/*
 * Various 8x8 bitmaps for menu display
 */

/*
 * Arcs for rounded boxes
 */
static const uint8_t top_left_arc[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0
};

static const uint8_t top_right_arc[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0
};

static const uint8_t bottom_left_arc[] = {
  0, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t bottom_right_arc[] = {
  0, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/*
 * Offset bar (right side)
 */
static const uint8_t offset_top[] = {
  0, 0, 0, 0, 0, 0, 0x0E, 0x1F
};

static const uint8_t offset_bottom[] = {
  0x1F, 0x0E, 0, 0, 0, 0, 0, 0
};

static const uint8_t offset_bar[] = {
  0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F
};

/*
 * Top and bottom of rounded box for snapshot list
 */
static const uint8_t top_half[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0
};

static const uint8_t bottom_half[] = {
  0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/*
 * Left/right part of selected line in snapshot list
 */
static const uint8_t left_blob[] = {
  0x1F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x1F
};

static const uint8_t right_blob[] = {
  0xF8, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xF8
};

/* ------------------------------------------------------------------------- */

/*
 * Offset data for display coordinates in display_ip_address()
 */
static const struct {
  uint8_t row_delta;
  uint8_t col;
} ip_address_offsets[] = {
  { 0, 1 }, { 0, 5 }, { 1, 1 }, { 1, 5 } /* assumes little-endian host (Z80) */
};


/* ------------------------------------------------------------------------- */

/*
 * Buffer for the loaded snapshot list. Located in a RAM area overwritten
 * by snapshot loading.
 */
static char __at(0x8000) menu_buffer[0x7800];

#define MAX_SNAPSHOTS   1024

/*
 * Array of pointers to 'cleaned' snapshot names (file extension removed,
 * NUL termination added).
 *
 * For some reason SDCC freaks out on the absolute addressing declaration
 * below, so I replaced it with an unreadable #define, which should behave
 * much the same way.
 *
 * static const char * __at(0xF800) snapshot_names[MAX_SNAPSHOTS];
 */
#define snapshot_names ((const char **) 0xF800)

/* ------------------------------------------------------------------------- */

/*
 * Number of snapshot names displayed at a time
 */
#define DISPLAY_LINES     (22)

/* ------------------------------------------------------------------------- */

/*
 * Indicates which file is currently being loaded. Used for making decisions
 * when loading fails in notify_tftp_error().
 */
static enum {
  EXPECTING_FILE_LIST,
  EXPECTING_NAMED_SNAPSHOT,
  EXPECTING_DEFAULT_SNAPSHOT
} expected_file = EXPECTING_FILE_LIST;

/* ------------------------------------------------------------------------- */

/*
 * Constant data for creating the menu screen
 */

/*
 * Attributes
 */
static const PACKED_STRUCT() {
  uint8_t attrs;
  uint8_t row;
  uint8_t col;
  uint8_t cells;
} attr_tab[] = {
  { INK(WHITE) | PAPER(BLACK) | BRIGHT, 2, 0, 10 }, 
  { INK(WHITE) | PAPER(BLACK), 4, 0, 10 },
  { INK(BLACK) | PAPER(WHITE), 0, 11, 20 },
  { INK(BLACK) | PAPER(BLUE), 1 + DISPLAY_LINES, 0, 9 },
  { INK(BLACK) | PAPER(WHITE), 1 + DISPLAY_LINES, 11, 20 }
};

/*
 * Individual patterns
 */
static const PACKED_STRUCT() {
  uint8_t row;
  uint8_t col;
  uint8_t *pattern;
} pattern_tab[] = {
  { 13, 4, FONTDATA_ADDRESS('.') },
  { 14, 4, FONTDATA_ADDRESS('.') },
  { 20, 4, FONTDATA_ADDRESS('.') },
  { 21, 4, FONTDATA_ADDRESS('.') },
  { 9, 0, top_left_arc },
  { 9, 8, top_right_arc },
  { 23, 0, bottom_left_arc },
  { 23, 8, bottom_right_arc },
  { 0, 11, top_left_arc },
  { 0, 30, top_right_arc },
  { 23, 11, bottom_left_arc },
  { 23, 30, bottom_right_arc }
};

#define NBR_ITEMS(_arr)     (sizeof(_arr) / sizeof(_arr[0]))

/* ========================================================================= */

/*
 * Display the indicated IP address (as a dotted 4-tuple), on two rows,
 * beginning on the row with address 'row_addr'. The IP address will be printed
 * in columns 25-31.
 *
 * Initial zeros of each byte will be omitted. No spaces will be printed
 * (that is, any clean-up of previous content is up to the caller).
 *
 * NOTE: due to simplified addressing of the video memory, the two lines
 * must NOT span over a 2K bank boundary.
 */
static void
display_ip_address(const ipv4_address_t *addr, uint8_t start_row)
{
  uint8_t i;
  const uint8_t *addr_bytes = (const uint8_t *) addr;
  
  for (i = 0; i < 4; i++) {
    uint8_t b = *addr_bytes++;
    uint8_t row = ip_address_offsets[i].row_delta + start_row;
    uint8_t col = ip_address_offsets[i].col;

    if (b >= 100) {
      print_char_at(row, col, '0' + (b / 100));
      b %= 100;
    }
    if (b >= 10) {
      print_char_at(row, col + 1, '0' + (b / 10));
      b %= 10;
    }
    print_char_at(row, col + 2, '0' + b);
  }
}

/* ------------------------------------------------------------------------- */

static void
display_screen(void)
{
  uint8_t i;
  
  print_at(2, 0, "SpeccyBoot");
  print_at(4, 0, VERSION);
  print_at(11, 1, "local:");
  print_at(18, 1, "server:");
  
  print_pattern_at(0, 31, offset_top);
  for (i = 1; i < (DISPLAY_LINES + 2); i++) {
    print_pattern_with_attrs_at(INK(BLACK) | PAPER(BLACK), i, 31, offset_bar);
  }
  print_pattern_at(23, 31, offset_bottom);
  
  for (i = 1; i < 31; i++) {
    print_pattern_with_attrs_at(INK(BLACK) | PAPER(BLACK),
                                0, i, top_half);
    print_pattern_with_attrs_at(INK(BLACK) | PAPER(BLACK),
                                1 + DISPLAY_LINES, i, bottom_half);
  }
  
  for (i = 0; i < 9; i++) {
    print_pattern_at(9, i, top_half);
  }
  
  for (i = 1; i < (1 + DISPLAY_LINES); i++) {
    print_pattern_with_attrs_at(INK(WHITE) | PAPER(WHITE), i, 11, left_blob);
    print_pattern_with_attrs_at(INK(WHITE) | PAPER(WHITE), i, 30, right_blob);
  }

  for (i = 0; i < NBR_ITEMS(pattern_tab); i++) {
    print_pattern_at(pattern_tab[i].row, pattern_tab[i].col, pattern_tab[i].pattern);
  }

  for (i = 0; i < NBR_ITEMS(attr_tab); i++) {
    set_attrs(attr_tab[i].attrs, attr_tab[i].row, attr_tab[i].col, attr_tab[i].cells);
  }
  
  for (i = 10; i < (1 + DISPLAY_LINES); i++) {
    set_attrs(INK(WHITE) | PAPER(BLUE), i, 0, 9);
  }
  
  for (i = 1; i < (1 + DISPLAY_LINES); i++) {
    set_attrs(INK(BLUE)  | PAPER(WHITE), i, 12, 18);
  }
}

/* ------------------------------------------------------------------------- */

static void
run_menu(uint8_t nbr_snapshots)
{
  uint16_t idx            = 0;
  uint16_t last_idx       = 1;    /* force update */
  
  uint16_t display_offset = 0;
  bool    needs_redraw    = true;
  uint8_t i;
  
  for (;;) {
    key_t key;
  
    if (idx != last_idx) {
      set_attrs(INK(WHITE) | PAPER(WHITE),
                1 + (last_idx - display_offset), 11, 1);
      set_attrs(INK(BLUE) | PAPER(WHITE),
                1 + (last_idx - display_offset), 12, 18);
      set_attrs(INK(WHITE) | PAPER(WHITE),
                1 + (last_idx - display_offset), 30, 1);
      
      if (nbr_snapshots > DISPLAY_LINES) {
        if ((idx < display_offset) || (idx >= (display_offset + DISPLAY_LINES)))
        {
          if (idx < (DISPLAY_LINES >> 1)) {
            display_offset = 0;
          }
          else if (idx > (nbr_snapshots - (DISPLAY_LINES >> 1))) {
            display_offset = nbr_snapshots - DISPLAY_LINES;
          }
          else {
            display_offset = (idx - (DISPLAY_LINES >> 1));
          }
          needs_redraw = true;
        }
        
        /*
         * Update offset bar
         */
        {
          uint8_t bar_length = (2 * (DISPLAY_LINES + 2) * (DISPLAY_LINES + 2) + nbr_snapshots)
                                / (nbr_snapshots << 1);
          uint8_t bar_offset = (2 * (DISPLAY_LINES + 2) * display_offset + nbr_snapshots)
                                / (nbr_snapshots << 1);

          for (i = 0; i < bar_offset; i++) {
            set_attrs(INK(WHITE) | PAPER(BLACK), i, 31, 1);
          }
          for (/* i = bar_offset */; i < (bar_offset + bar_length); i++) {
            set_attrs(INK(BLUE) | PAPER(BLACK) | BRIGHT, i, 31, 1);
          }
          for (/* i = bar_offset + bar_length */; i < (DISPLAY_LINES + 2); i++) {
            set_attrs(INK(WHITE) | PAPER(BLACK), i, 31, 1);
          }
        }
      }
      
      set_attrs(INK(BLACK) | PAPER(WHITE),
                1 + (idx - display_offset), 11, 1);
      set_attrs(INK(WHITE) | PAPER(BLACK) | BRIGHT,
                1 + (idx - display_offset), 12, 18);      
      set_attrs(INK(BLACK) | PAPER(WHITE),
                1 + (idx - display_offset), 30, 1);

      last_idx = idx;
    }
    
    if (needs_redraw) {
      uint8_t i;
      for (i = 0;
           (i < DISPLAY_LINES) && ((i + display_offset) < nbr_snapshots);
           i++)
      {
        const char *s = snapshot_names[i + display_offset];
        uint8_t col = 12;
        while ((*s) && (*s != '.') && (col < 30)) {
          print_char_at(i + 1, col++, *s++);
        }
        while (col < 30) {
          print_char_at(i + 1, col++, ' ');
        }
      }
      
      needs_redraw = false;
    }
    
    key = wait_key();
    
    switch(key) {
      case KEY_ENTER:
        key_click();
        cls();
        expected_file = EXPECTING_NAMED_SNAPSHOT;
        z80_expect_snapshot();
        syslog("loading selected snapshot");
        tftp_read_request(snapshot_names[idx]);
        return;
      case KEY_UP:
        if (idx > 0) {
          key_click();
          idx --;
        }
        break;
      case KEY_DOWN:
        if (idx < (nbr_snapshots - 1)) {
          key_click();
          idx ++;
        }
        break;
      default:
      {
        uint8_t i;
        for (i = 0; i < nbr_snapshots; i++) {
          char ch = snapshot_names[i][0];
          if (ch >= 'a' && ch <= 'z') {
            ch &= 0xDF;     /* upper case */
          }
          if (ch >= (int) key) {
            key_click();
            idx = i;
            break;
          }
        }
      }
    }
  }
}

/* ------------------------------------------------------------------------- */

/*
 * Called by DHCP (see dhcp.h) when an IP address has been obtained
 */
void
notify_dhcp_bound(void)
{
#ifdef EMULATOR_TEST
  static const ipv4_address_t local = ntohl(0xC0A80205);
  display_ip_address(&local, 13);
#else
  syslog("IP address configured by DHCP");
  display_ip_address(&ip_config.host_address, 13);
#endif
  
  z80_expect_raw_data(menu_buffer, sizeof(menu_buffer));
  tftp_read_request(IMAGE_LIST_FILE);
}

/* ------------------------------------------------------------------------- */

/*
 * Called by the file loader when a data file (specifically, the snapshot
 * list) has been loaded.
 */
void notify_file_loaded(const void *end_addr)
{
  char *src = menu_buffer;
  uint16_t max_idx = 0;
  
#ifdef EMULATOR_TEST
  /*
   * Just make up some IP addresses when testing in emulator
   */
  static const ipv4_address_t server = ntohl(0xC0A80202);
    
  display_ip_address(&server, 20);
#else  
  /*
   * The sender of the most recent packet (final TFTP DATA packet) is
   * the TFTP server, so just pick it from the receive buffer.
   */
  syslog("menu loaded");
  display_ip_address(&rx_frame.ip.src_addr, 20);
#endif
  
  while ((src < end_addr) && (max_idx < MAX_SNAPSHOTS)) {
    snapshot_names[max_idx ++] = src;
    
    /*
     * Find file extension, replace with NUL termination
     */
    while (src < end_addr) {
      unsigned char c = (unsigned char) *src;
      if ((int) c < ' ') {
        *src = '\0';
        do {
          src ++;
        } while ((src < end_addr) && (*src < ' '));
        break;
      }
      else {
        src ++;
      }
    }
  }
  
  run_menu(max_idx);
}

/* ------------------------------------------------------------------------- */

/*
 * Called by TFTP when a file loading operation failed.
 */
void notify_tftp_error(void)
{
  if (expected_file != EXPECTING_FILE_LIST) {
    fatal_error("file not found");
  }
  
  /*
   * File list loading failed: fall back on a standard snapshot image
   */
  cls();
  expected_file = EXPECTING_DEFAULT_SNAPSHOT;
  z80_expect_snapshot();
  syslog("no menu found, trying default snapshot");
  tftp_read_request(DEFAULT_SNAPSHOT_FILE);
}

/* ------------------------------------------------------------------------- */

void main(void)
{
  cls();
  
  set_border(BLACK);
  
  load_font_data();

  display_screen();
  
#ifdef EMULATOR_TEST
  notify_dhcp_bound();                    /* fake DHCP event */
#else
  eth_init();
  dhcp_init();
  eth_handle_incoming_frames();
#endif
}