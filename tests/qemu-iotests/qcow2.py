#!/usr/bin/env python3
#
# Manipulations with qcow2 image
#
# Copyright (C) 2012 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import sys

from qcow2_format import (
    QcowHeader,
    QcowHeaderExtension
)


is_json = False


def cmd_dump_header(fd):
    h = QcowHeader(fd)
    h.dump(is_json)
    print()
    h.dump_extensions(is_json)


def cmd_dump_header_exts(fd):
    h = QcowHeader(fd)
    h.dump_extensions(is_json)


def cmd_set_header(fd, name, value):
    try:
        value = int(value, 0)
    except ValueError:
        print("'%s' is not a valid number" % value)
        sys.exit(1)

    fields = (field[2] for field in QcowHeader.fields)
    if name not in fields:
        print("'%s' is not a known header field" % name)
        sys.exit(1)

    h = QcowHeader(fd)
    h.__dict__[name] = value
    h.update(fd)


def cmd_add_header_ext(fd, magic, data):
    try:
        magic = int(magic, 0)
    except ValueError:
        print("'%s' is not a valid magic number" % magic)
        sys.exit(1)

    h = QcowHeader(fd)
    h.extensions.append(QcowHeaderExtension.create(magic,
                                                   data.encode('ascii')))
    h.update(fd)


def cmd_add_header_ext_stdio(fd, magic):
    data = sys.stdin.read()
    cmd_add_header_ext(fd, magic, data)


def cmd_del_header_ext(fd, magic):
    try:
        magic = int(magic, 0)
    except ValueError:
        print("'%s' is not a valid magic number" % magic)
        sys.exit(1)

    h = QcowHeader(fd)
    found = False

    for ex in h.extensions:
        if ex.magic == magic:
            found = True
            h.extensions.remove(ex)

    if not found:
        print("No such header extension")
        return

    h.update(fd)


def cmd_set_feature_bit(fd, group, bit):
    try:
        bit = int(bit, 0)
        if bit < 0 or bit >= 64:
            raise ValueError
    except ValueError:
        print("'%s' is not a valid bit number in range [0, 64)" % bit)
        sys.exit(1)

    h = QcowHeader(fd)
    if group == 'incompatible':
        h.incompatible_features |= 1 << bit
    elif group == 'compatible':
        h.compatible_features |= 1 << bit
    elif group == 'autoclear':
        h.autoclear_features |= 1 << bit
    else:
        print("'%s' is not a valid group, try "
              "'incompatible', 'compatible', or 'autoclear'" % group)
        sys.exit(1)

    h.update(fd)


cmds = [
    ['dump-header', cmd_dump_header, 0,
     'Dump image header and header extensions'],
    ['dump-header-exts', cmd_dump_header_exts, 0,
     'Dump image header extensions'],
    ['set-header', cmd_set_header, 2, 'Set a field in the header'],
    ['add-header-ext', cmd_add_header_ext, 2, 'Add a header extension'],
    ['add-header-ext-stdio', cmd_add_header_ext_stdio, 1,
     'Add a header extension, data from stdin'],
    ['del-header-ext', cmd_del_header_ext, 1, 'Delete a header extension'],
    ['set-feature-bit', cmd_set_feature_bit, 2, 'Set a feature bit'],
]


def main(filename, cmd, args):
    fd = open(filename, "r+b")
    try:
        for name, handler, num_args, desc in cmds:
            if name != cmd:
                continue
            elif len(args) != num_args:
                usage()
                return
            else:
                handler(fd, *args)
                return
        print("Unknown command '%s'" % cmd)
    finally:
        fd.close()


def usage():
    print("Usage: %s <file> <cmd> [<arg>, ...] [<key>, ...]" % sys.argv[0])
    print("")
    print("Supported commands:")
    for name, handler, num_args, desc in cmds:
        print("    %-20s - %s" % (name, desc))
    print("")
    print("Supported keys:")
    print("    %-20s - %s" % ('-j', 'Dump in JSON format'))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()
        sys.exit(1)

    is_json = '-j' in sys.argv
    if is_json:
        sys.argv.remove('-j')

    main(sys.argv[1], sys.argv[2], sys.argv[3:])
