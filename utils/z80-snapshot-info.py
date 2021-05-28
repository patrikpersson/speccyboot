#!/usr/bin/env python2

# z80-snapshot-info.py
#
# Simple script to inspect the contents of a .Z80 snapshot.
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
#
# ----------------------------------------------------------------------------
#
# Copyright (c) 2009-  Patrik Persson
# 
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

import sys
import os.path
import struct
import string

byte_regs = {}
word_regs = {}

def flush_line():
  global hex_seq
  global ascii_seq
  global curr_offset
  global offset_of_curr_line

  print ("   %04x: " % offset_of_curr_line), hex_seq, '  ', ascii_seq
  hex_seq   = ""
  ascii_seq = ""
  offset_of_curr_line = curr_offset

# -----------------------------------------------------------------------------

def display_byte(b):
  global hex_seq
  global ascii_seq
  global curr_offset
  
  hex_seq += (" %02x" % ord(b))
  
  if ord(b) >= 32 and string.count(string.printable, b) > 0:
    ascii_seq += b
  else:
    ascii_seq += '.'

  curr_offset += 1
  if (curr_offset % 16) == 0: flush_line()

# -----------------------------------------------------------------------------

def display_uncompressed_data(bytes):
  global hex_seq
  global ascii_seq
  global curr_offset
  global offset_of_curr_line

  hex_seq             = ""
  ascii_seq           = ""
  curr_offset         = 0
  offset_of_curr_line = 0
  
  for i in range(len(bytes)):
    display_byte(bytes[i])

# -----------------------------------------------------------------------------

def display_compressed_data(bytes):
  global hex_seq
  global ascii_seq
  global curr_offset
  global offset_of_curr_line

  hex_seq             = ""
  ascii_seq           = ""
  curr_offset         = 0
  offset_of_curr_line = 0
  
  i = 0
  while i < len(bytes):
    b = bytes[i]
    i += 1
    if ord(b) == 0xED and i < len(bytes) and ord(bytes[i]) == 0xED:
      # found a sequence
      for j in range(ord(bytes[i + 1])): display_byte(bytes[i + 2])
      i += 3
    else:
      display_byte(b)

# -----------------------------------------------------------------------------

def usage():
  print "usage:"
  print "  %s [-v] [-h] <some_snapshot.z80>" % os.path.basename(sys.argv[0])
  print ""
  print "  -h: display this message"
  print "  -v: verbose (dump memory bank contents)"
  exit(1)

# -----------------------------------------------------------------------------

if len(sys.argv) < 2: usage()

verbose    = False
in_file    = None

for arg in sys.argv[1:]:
  if arg == '-h':
    usage()
  elif arg == '-v':
    verbose = True
  else:
    if in_file:
      usage()
    in_file = open(arg)

(byte_regs['a'], byte_regs['f'], word_regs['bc'], word_regs['hl'],
 word_regs['pc'], word_regs['sp'], byte_regs['i'], byte_regs['r'], flags,
 word_regs['de'], word_regs["bc'"], word_regs["de'"], word_regs["hl'"],
 byte_regs["a'"], byte_regs["f'"], word_regs['ix'], word_regs['iy'],
 iff1, iff2, imode) = struct.unpack('<BBHHHHBBBHHHHBBHHBBB', in_file.read(30))

if word_regs['pc'] == 0:     # version 2 or 3 sub-header follows
  (sub_header_length,) = struct.unpack('<H', in_file.read(2))
  (word_regs['pc'], hw_type, hw_state,
   if1_flag, hw_flags) = struct.unpack('<HBBBB', in_file.read(6))
  in_file.read(sub_header_length - 6)    # ignore remaining sub-header
  if sub_header_length == 23:
    version = 2
    hw_desc = ('48k', '48k + IF1', 'SamRam', '128k', '128k + IF1')[hw_type]
    nbr_banks = (3, 3, 5, 8, 8)[hw_type]
  else:
    version = 3
    hw_desc = ('48k', '48k + IF1', 'SamRam', '48k + MGT',
               '128k', '128k + IF1', '128k + MGT')[hw_type]
    nbr_banks = (3, 3, 5, 3, 8, 8, 8)[hw_type]
  if hw_flags & 0x80:
    if hw_desc == '48k':
      hw_desc   = '16k'
    elif hw_desc == '128k':
      hw_desc = '128k +2'
else:
  version = 1
  hw_desc = '48k'

print "snapshot format version %s" % version
print " hardware: %s" % hw_desc

if version > 1  and  nbr_banks == 8:    # 128k/+2/+3
  print " 128k paging state:",
  print "0x%02x (page %d at 0xc000, display page %d, ROM%d, %s)" % (
    hw_state,
    (hw_state & 0x07),
    (7 if (hw_state & 0x08) else 5),
    (1 if (hw_state & 0x10) else 0),
    ("locked" if (hw_state & 0x20) else "unlocked")
  )

# check compatibility
if hw_desc == '128k':
  if hw_state & 0x08:
    print "incompatible snapshot: screen at page 7"
elif hw_desc != '48k' and hw_desc != '16k':
  print "incompatible snapshot: unsupported configuration: %s" % hw_desc

print "registers:"
for reg_name in ('a', 'f', 'i', 'r', "a'", "f'"):
  print " %-2s  = 0x%02x" % (reg_name, byte_regs[reg_name])
for reg_name in ('pc', 'sp', 'bc', 'de', 'hl', 'ix', 'iy', "bc'", "de'", "hl'"):
  print " %-3s = 0x%04x" % (reg_name, word_regs[reg_name])

print "memory snapshot format:"
if version == 1:
  if flags == 0xff or (flags & 0x20) == 0:
    print " single 48k uncompressed block"
    data = in_file.read(0xc000)
    if verbose: display_uncompressed_data(data)
  else:
    print " single 48k compressed block"
    data = in_file.read()
    if verbose: display_compressed_data(data)
else:
  print " %d x 16k pages:" % nbr_banks
  for i in range(nbr_banks):
    (page_data_length, page_id) = struct.unpack('<HB', in_file.read(3))
    if page_data_length == 0xffff:
      print "  page %d, uncompressed" % page_id
      data = in_file.read(0x4000)
      if verbose: display_uncompressed_data(data)
    else:
      print "  page %d, compressed (%d bytes)" % (page_id, page_data_length)
      data = in_file.read(page_data_length)
      if verbose: display_compressed_data(data)
