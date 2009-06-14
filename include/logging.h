/*
 * Module logging:
 *
 * BSD-style syslog support (RFC 3164)
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

#ifndef SPECCYBOOT_LOGGING_INCLUSION_GUARD
#define SPECCYBOOT_LOGGING_INCLUSION_GUARD

/* -------------------------------------------------------------------------
 *
 * Logging using log_info(), log_warning(), log_error(). Work like printf,
 * but with special format specifiers:
 *
 * %x       native endian, 16 bits
 * %b       8 bits
 * %a       pointer to IP address (32 bits, network order)
 * %s       pointer to NUL-terminated string
 * ------------------------------------------------------------------------- */

#ifdef UDP_LOGGING_SERVER_IP_ADDRESS
#define UDP_LOGGING
#endif

#ifdef UDP_LOGGING

/*
 * All log messages are labeled as 'kernel', so we can use the resulting
 * character directly
 */
#define log_emergency(_tag, ...)          _log_udp_msg('0', _tag, __VA_ARGS__)
#define log_error(_tag, ...)              _log_udp_msg('3', _tag, __VA_ARGS__)
#define log_warning(_tag, ...)            _log_udp_msg('4', _tag, __VA_ARGS__)
#define log_info(_tag, ...)               _log_udp_msg('6', _tag, __VA_ARGS__)

void
_log_udp_msg(char severity, const char *tag, const char *fmt, ...);

#else

#define log_emergency(...)
#define log_error(...)
#define log_warning(...)
#define log_info(...)

#endif

#endif /* SPECCYBOOT_LOGGING_INCLUSION_GUARD */
