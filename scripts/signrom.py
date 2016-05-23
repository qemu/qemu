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
if magic != '\x55\xaa':
    sys.exit("%s: option ROM does not begin with magic 55 aa" % sys.argv[1])

size_byte = ord(fin.read(1))
fin.seek(0)

if size_byte == 0:
    # If the caller left the size field blank then we will fill it in,
    # also rounding the whole input to a multiple of 512 bytes.
    data = fin.read()
    # +1 because we need a byte to store the checksum.
    size = len(data) + 1
    # Round up to next multiple of 512.
    size += 511
    size -= size % 512
    if size >= 65536:
        sys.exit("%s: option ROM size too large" % sys.argv[1])
    # size-1 because a final byte is added below to store the checksum.
    data = data.ljust(size-1, '\0')
    data = data[:2] + chr(size/512) + data[3:]
else:
    # Otherwise the input file specifies the size so use it.
    # -1 because we overwrite the last byte of the file with the checksum.
    size = size_byte * 512 - 1
    data = fin.read(size)

fout.write(data)

checksum = 0
for b in data:
    # catch Python 2 vs. 3 differences
    if isinstance(b, int):
        checksum += b
    else:
        checksum += ord(b)
checksum = (256 - checksum) % 256

# Python 3 no longer allows chr(checksum)
fout.write(struct.pack('B', checksum))

fin.close()
fout.close()
