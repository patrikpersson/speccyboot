/*
 * Module params:
 *
 * Read access to system parameters (Ethernet/IP addresses, ...)
 *
 * Part of the SpeccyBoot project <http://speccyboot.sourceforge.org>
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

#include <string.h>

#include "params.h"

/* ========================================================================= */

bool
params_valid(void)
{
  return true;
}

/* ------------------------------------------------------------------------- */

void
params_get_mac_address(mac_address_t *mac_address)
{
  static const mac_address_t a = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc };
  memcpy(mac_address, a, sizeof(mac_address_t));
}

/* ------------------------------------------------------------------------- */

void
params_get_host_address(ipv4_address_t *host_address)
{
  static const ipv4_address_t a = { 0xc0, 0xa8, 0x00, 0xf0 };
  memcpy(host_address, a, sizeof(ipv4_address_t));
}

/* ------------------------------------------------------------------------- */

void
params_get_server_address(ipv4_address_t *server_address)
{
  static const ipv4_address_t a = { 0xc0, 0xa8, 0x00, 0x0a };
  memcpy(server_address, a, sizeof(ipv4_address_t));
}


