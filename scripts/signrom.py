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

fin.seek(2)
size = ord(fin.read(1)) * 512 - 1

fin.seek(0)
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
