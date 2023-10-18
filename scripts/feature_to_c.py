#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import os, sys

def writeliteral(indent, bytes):
    sys.stdout.write(' ' * indent)
    sys.stdout.write('"')
    quoted = True

    for c in bytes:
        if not quoted:
            sys.stdout.write('\n')
            sys.stdout.write(' ' * indent)
            sys.stdout.write('"')
            quoted = True

        if c == b'"'[0]:
            sys.stdout.write('\\"')
        elif c == b'\\'[0]:
            sys.stdout.write('\\\\')
        elif c == b'\n'[0]:
            sys.stdout.write('\\n"')
            quoted = False
        elif c >= 32 and c < 127:
            sys.stdout.write(c.to_bytes(1, 'big').decode())
        else:
            sys.stdout.write(f'\{c:03o}')

    if quoted:
        sys.stdout.write('"')

sys.stdout.write('#include "qemu/osdep.h"\n' \
                 '#include "exec/gdbstub.h"\n' \
                 '\n'
                 'const GDBFeature gdb_static_features[] = {\n')

for input in sys.argv[1:]:
    with open(input, 'rb') as file:
        read = file.read()

    sys.stdout.write('    {\n')
    writeliteral(8, bytes(os.path.basename(input), 'utf-8'))
    sys.stdout.write(',\n')
    writeliteral(8, read)
    sys.stdout.write('\n    },\n')

sys.stdout.write('    { NULL }\n};\n')
