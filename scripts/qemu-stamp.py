#! /usr/bin/env python3

# Usage: scripts/qemu-stamp.py STRING1 STRING2... -- FILE1 FILE2...
import hashlib
import os
import sys

sha = hashlib.sha1()
is_file = False
for arg in sys.argv[1:]:
    if arg == '--':
        is_file = True
        continue
    if is_file:
        with open(arg, 'rb') as f:
            for chunk in iter(lambda: f.read(65536), b''):
                sha.update(chunk)
    else:
        sha.update(os.fsencode(arg))
        sha.update(b'\n')

# The hash can start with a digit, which the compiler doesn't
# like as an symbol. So prefix it with an underscore
print("_" + sha.hexdigest())
