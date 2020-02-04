#!/usr/bin/env python3

#
# Option ROM signing utility
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

import sys
import struct

if len(sys.argv) < 3:
    print('usage: signrom.py input output')
    sys.exit(1)

fin = open(sys.argv[1], 'rb')
fout = open(sys.argv[2], 'wb')

magic = fin.read(2)
if magic != b'\x55\xaa':
    sys.exit("%s: option ROM does not begin with magic 55 aa" % sys.argv[1])

size_byte = ord(fin.read(1))
fin.seek(0)
data = fin.read()

size = size_byte * 512
if len(data) > size:
    sys.stderr.write('error: ROM is too large (%d > %d)\n' % (len(data), size))
    sys.exit(1)
elif len(data) < size:
    # Add padding if necessary, rounding the whole input to a multiple of
    # 512 bytes according to the third byte of the input.
    # size-1 because a final byte is added below to store the checksum.
    data = data.ljust(size-1, b'\0')
else:
    if ord(data[-1:]) != 0:
        sys.stderr.write('WARNING: ROM includes nonzero checksum\n')
    data = data[:size-1]

fout.write(data)

checksum = 0
for b in data:
    checksum = (checksum - b) & 255

fout.write(struct.pack('B', checksum))

fin.close()
fout.close()
