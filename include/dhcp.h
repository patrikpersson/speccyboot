/*
 * Module dhcp:
 *
 * Dynamic Host Configuration Protocol (DHCP, RFC 2131)
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
#ifndef SPECCYBOOT_DHCP_INCLUSION_GUARD
#define SPECCYBOOT_DHCP_INCLUSION_GUARD

#include "udp.h"

/* ------------------------------------------------------------------------- */

/*
 * DHCP state
 */
enum dhcp_state_t {
  STATE_INIT,
  STATE_SELECTING,
  STATE_REQUESTING,
  STATE_BOUND
};

/* ------------------------------------------------------------------------- */

/*
 * Notification callback:
 *
 * called when the states SELECTING/REQUESTING/BOUND states are entered.
 */
#define NOTIFY_DHCP_STATE         notify_dhcp_state

/*
 * Prototype for callback (the actual function name is #define'd in above)
 */
void NOTIFY_DHCP_STATE(enum dhcp_state_t state);

/* ------------------------------------------------------------------------- */

struct dhcp_header_t;

/* -------------------------------------------------------------------------
 * Called by when a DHCP packet has been received
 * ------------------------------------------------------------------------- */

void
dhcp_packet_received(const ipv4_address_t        *src,
                     const struct dhcp_header_t  *packet);

/* -------------------------------------------------------------------------
 * Obtain an IP address using DHCP.
 *
 * When an address has been obtained, the function referenced by the
 * DHCP_COMPLETED_HANDLER macro is called.
 * ------------------------------------------------------------------------- */

void
dhcp_init(void);

#endif /* SPECCYBOOT_DHCP_INCLUSION_GUARD */
