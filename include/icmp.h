/*
 * Module icmp:
 *
 * Internet Control Message Protocol (ICMP, RFC 792)
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
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
#ifndef SPECCYBOOT_ICMP_INCLUSION_GUARD
#define SPECCYBOOT_ICMP_INCLUSION_GUARD

#include <stdint.h>

#include "eth.h"
#include "ip.h"
#include "spectrum.h"

/* -------------------------------------------------------------------------
 * Called by IP when an ICMP packet has been identified
 * ------------------------------------------------------------------------- */
void
icmp_packet_received(const struct mac_address_t  *src_hwaddr,
                     const ipv4_address_t        *src,
                     const uint8_t               *payload,
                     uint16_t                     nbr_bytes_in_payload);

#endif /* SPECCYBOOT_ICMP_INCLUSION_GUARD */