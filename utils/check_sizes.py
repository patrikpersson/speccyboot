#!/usr/bin/env python

# check_sizes.py
#
# Simple script to check that code and read/write data segments are within
# the allowed sizes.
#
# Usage: check_sizes.py <symbol-dump-file>, e.g.,
#        check_sizes.py obj/speccyboot.sym
#
# Part of the SpeccyBoot project <http://speccyboot.sourceforge.net>
#
# ----------------------------------------------------------------------------
#
# Copyright (c) 2009, Patrik Persson
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

# Maximal addresses for end of code and read/write data segments, respectively

limits = {
  'end_of_code':    0x2000,
  'end_of_data':    0x6000
}

if len(sys.argv) != 2:
  print "missing file name to symbol list"
  sys.exit(1)

for line in open(sys.argv[1], 'r').readlines():
  if line[0] == ';': continue
  addr = int(line[3:7], 16)
  symbol = line[8:].strip()

  for s in limits.keys():
    if s == symbol:
      if limits[s] < addr:
        print "symbol %s out of range: 0x%x" % (s, addr)
        exit(1)
      else:
        print "%s OK, %d bytes left" % (s[7:], (limits[s] - addr))
