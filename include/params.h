/*
 * Module params:
 *
 * Read access to system parameters (Ethernet/IP addresses, ...)
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009, Patrik Persson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of SpeccyBoot nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PATRIK PERSSON ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PATRIK PERSSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef SPECCYBOOT_PARAMS_INCLUSION_GUARD
#define SPECCYBOOT_PARAMS_INCLUSION_GUARD

#include <stdbool.h>
#include <stdint.h>

/* ========================================================================= */

#define SIZEOF_MAC_ADDRESS      (6)
#define SIZEOF_IPV4_ADDRESS     (4)

/* ------------------------------------------------------------------------- */

typedef uint8_t mac_address_t[SIZEOF_MAC_ADDRESS];
typedef uint8_t ipv4_address_t[SIZEOF_IPV4_ADDRESS];

/* ========================================================================= */

/* -------------------------------------------------------------------------
 * Returns true if parameters are set (checksum is OK), false otherwise.
 *
 * If this function returns false, the remaining functions will return
 * garbage (and thus should not be called at all).
 * ------------------------------------------------------------------------- */

bool
params_valid(void);

/* -------------------------------------------------------------------------
 * Returns (in output argument) the configured MAC address.
 * ------------------------------------------------------------------------- */

void
params_get_mac_address(mac_address_t *mac_address);

/* -------------------------------------------------------------------------
 * Returns (in output argument) the configured IPV4 host address.
 * ------------------------------------------------------------------------- */

void
params_get_host_address(ipv4_address_t *host_address);

/* -------------------------------------------------------------------------
 * Returns (in output argument) the configured IPV4 server address.
 * ------------------------------------------------------------------------- */

void
params_get_server_address(ipv4_address_t *server_address);

#endif /* SPECCYBOOT_PARAMS_INCLUSION_GUARD */
