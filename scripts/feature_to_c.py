#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import os, sys, xml.etree.ElementTree

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

    parser = xml.etree.ElementTree.XMLPullParser(['start', 'end'])
    parser.feed(read)
    events = parser.read_events()
    event, element = next(events)
    if event != 'start':
        sys.stderr.write(f'unexpected event: {event}\n')
        exit(1)
    if element.tag != 'feature':
        sys.stderr.write(f'unexpected start tag: {element.tag}\n')
        exit(1)

    feature_name = element.attrib['name']
    regnum = 0
    regnames = []
    regnums = []
    tags = ['feature']
    for event, element in events:
        if event == 'end':
            if element.tag != tags[len(tags) - 1]:
                sys.stderr.write(f'unexpected end tag: {element.tag}\n')
                exit(1)

            tags.pop()
            if element.tag == 'feature':
                break
        elif event == 'start':
            if len(tags) < 2 and element.tag == 'reg':
                if 'regnum' in element.attrib:
                    regnum = int(element.attrib['regnum'])

                regnames.append(element.attrib['name'])
                regnums.append(regnum)
                regnum += 1

            tags.append(element.tag)
        else:
            raise Exception(f'unexpected event: {event}\n')

    if len(tags):
        sys.stderr.write('unterminated feature tag\n')
        exit(1)

    base_reg = min(regnums)
    num_regs = max(regnums) - base_reg + 1 if len(regnums) else 0

    sys.stdout.write('    {\n')
    writeliteral(8, bytes(os.path.basename(input), 'utf-8'))
    sys.stdout.write(',\n')
    writeliteral(8, read)
    sys.stdout.write(',\n')
    writeliteral(8, bytes(feature_name, 'utf-8'))
    sys.stdout.write(',\n        (const char * const []) {\n')

    for index, regname in enumerate(regnames):
        sys.stdout.write(f'            [{regnums[index] - base_reg}] =\n')
        writeliteral(16, bytes(regname, 'utf-8'))
        sys.stdout.write(',\n')

    sys.stdout.write(f'        }},\n        {num_regs},\n    }},\n')

sys.stdout.write('    { NULL }\n};\n')
