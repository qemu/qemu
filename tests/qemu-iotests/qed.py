#!/usr/bin/env python
#
# Tool to manipulate QED image files
#
# Copyright (C) 2010 IBM, Corp.
#
# Authors:
#  Stefan Hajnoczi <stefanha@linux.vnet.ibm.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.

import sys
import struct
import random
import optparse

# This can be used as a module
__all__ = ['QED_F_NEED_CHECK', 'QED']

QED_F_NEED_CHECK = 0x02

header_fmt = '<IIIIQQQQQII'
header_size = struct.calcsize(header_fmt)
field_names = ['magic', 'cluster_size', 'table_size',
               'header_size', 'features', 'compat_features',
               'autoclear_features', 'l1_table_offset', 'image_size',
               'backing_filename_offset', 'backing_filename_size']
table_elem_fmt = '<Q'
table_elem_size = struct.calcsize(table_elem_fmt)

def err(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(1)

def unpack_header(s):
    fields = struct.unpack(header_fmt, s)
    return dict((field_names[idx], val) for idx, val in enumerate(fields))

def pack_header(header):
    fields = tuple(header[x] for x in field_names)
    return struct.pack(header_fmt, *fields)

def unpack_table_elem(s):
    return struct.unpack(table_elem_fmt, s)[0]

def pack_table_elem(elem):
    return struct.pack(table_elem_fmt, elem)

class QED(object):
    def __init__(self, f):
        self.f = f

        self.f.seek(0, 2)
        self.filesize = f.tell()

        self.load_header()
        self.load_l1_table()

    def raw_pread(self, offset, size):
        self.f.seek(offset)
        return self.f.read(size)

    def raw_pwrite(self, offset, data):
        self.f.seek(offset)
        return self.f.write(data)

    def load_header(self):
        self.header = unpack_header(self.raw_pread(0, header_size))

    def store_header(self):
        self.raw_pwrite(0, pack_header(self.header))

    def read_table(self, offset):
        size = self.header['table_size'] * self.header['cluster_size']
        s = self.raw_pread(offset, size)
        table = [unpack_table_elem(s[i:i + table_elem_size]) for i in xrange(0, size, table_elem_size)]
        return table

    def load_l1_table(self):
        self.l1_table = self.read_table(self.header['l1_table_offset'])
        self.table_nelems = self.header['table_size'] * self.header['cluster_size'] / table_elem_size

    def write_table(self, offset, table):
        s = ''.join(pack_table_elem(x) for x in table)
        self.raw_pwrite(offset, s)

def random_table_item(table):
    vals = [(index, offset) for index, offset in enumerate(table) if offset != 0]
    if not vals:
        err('cannot pick random item because table is empty')
    return random.choice(vals)

def corrupt_table_duplicate(table):
    '''Corrupt a table by introducing a duplicate offset'''
    victim_idx, victim_val = random_table_item(table)
    unique_vals = set(table)
    if len(unique_vals) == 1:
        err('no duplication corruption possible in table')
    dup_val = random.choice(list(unique_vals.difference([victim_val])))
    table[victim_idx] = dup_val

def corrupt_table_invalidate(qed, table):
    '''Corrupt a table by introducing an invalid offset'''
    index, _ = random_table_item(table)
    table[index] = qed.filesize + random.randint(0, 100 * 1024 * 1024 * 1024 * 1024)

def cmd_show(qed, *args):
    '''show [header|l1|l2 <offset>]- Show header or l1/l2 tables'''
    if not args or args[0] == 'header':
        print qed.header
    elif args[0] == 'l1':
        print qed.l1_table
    elif len(args) == 2 and args[0] == 'l2':
        offset = int(args[1])
        print qed.read_table(offset)
    else:
        err('unrecognized sub-command')

def cmd_duplicate(qed, table_level):
    '''duplicate l1|l2 - Duplicate a random table element'''
    if table_level == 'l1':
        offset = qed.header['l1_table_offset']
        table = qed.l1_table
    elif table_level == 'l2':
        _, offset = random_table_item(qed.l1_table)
        table = qed.read_table(offset)
    else:
        err('unrecognized sub-command')
    corrupt_table_duplicate(table)
    qed.write_table(offset, table)

def cmd_invalidate(qed, table_level):
    '''invalidate l1|l2 - Plant an invalid table element at random'''
    if table_level == 'l1':
        offset = qed.header['l1_table_offset']
        table = qed.l1_table
    elif table_level == 'l2':
        _, offset = random_table_item(qed.l1_table)
        table = qed.read_table(offset)
    else:
        err('unrecognized sub-command')
    corrupt_table_invalidate(qed, table)
    qed.write_table(offset, table)

def cmd_need_check(qed, *args):
    '''need-check [on|off] - Test, set, or clear the QED_F_NEED_CHECK header bit'''
    if not args:
        print bool(qed.header['features'] & QED_F_NEED_CHECK)
        return

    if args[0] == 'on':
        qed.header['features'] |= QED_F_NEED_CHECK
    elif args[0] == 'off':
        qed.header['features'] &= ~QED_F_NEED_CHECK
    else:
        err('unrecognized sub-command')
    qed.store_header()

def cmd_zero_cluster(qed, pos, *args):
    '''zero-cluster <pos> [<n>] - Zero data clusters'''
    pos, n = int(pos), 1
    if args:
        if len(args) != 1:
            err('expected one argument')
        n = int(args[0])

    for i in xrange(n):
        l1_index = pos / qed.header['cluster_size'] / len(qed.l1_table)
        if qed.l1_table[l1_index] == 0:
            err('no l2 table allocated')

        l2_offset = qed.l1_table[l1_index]
        l2_table = qed.read_table(l2_offset)

        l2_index = (pos / qed.header['cluster_size']) % len(qed.l1_table)
        l2_table[l2_index] = 1 # zero the data cluster
        qed.write_table(l2_offset, l2_table)
        pos += qed.header['cluster_size']

def cmd_copy_metadata(qed, outfile):
    '''copy-metadata <outfile> - Copy metadata only (for scrubbing corrupted images)'''
    out = open(outfile, 'wb')

    # Match file size
    out.seek(qed.filesize - 1)
    out.write('\0')

    # Copy header clusters
    out.seek(0)
    header_size_bytes = qed.header['header_size'] * qed.header['cluster_size']
    out.write(qed.raw_pread(0, header_size_bytes))

    # Copy L1 table
    out.seek(qed.header['l1_table_offset'])
    s = ''.join(pack_table_elem(x) for x in qed.l1_table)
    out.write(s)

    # Copy L2 tables
    for l2_offset in qed.l1_table:
        if l2_offset == 0:
            continue
        l2_table = qed.read_table(l2_offset)
        out.seek(l2_offset)
        s = ''.join(pack_table_elem(x) for x in l2_table)
        out.write(s)

    out.close()

def usage():
    print 'Usage: %s <file> <cmd> [<arg>, ...]' % sys.argv[0]
    print
    print 'Supported commands:'
    for cmd in sorted(x for x in globals() if x.startswith('cmd_')):
        print globals()[cmd].__doc__
    sys.exit(1)

def main():
    if len(sys.argv) < 3:
        usage()
    filename, cmd = sys.argv[1:3]

    cmd = 'cmd_' + cmd.replace('-', '_')
    if cmd not in globals():
        usage()

    qed = QED(open(filename, 'r+b'))
    try:
        globals()[cmd](qed, *sys.argv[3:])
    except TypeError, e:
        sys.stderr.write(globals()[cmd].__doc__ + '\n')
        sys.exit(1)

if __name__ == '__main__':
    main()
