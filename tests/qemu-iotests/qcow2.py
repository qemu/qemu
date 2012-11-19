#!/usr/bin/env python

import sys
import struct
import string

class QcowHeaderExtension:

    def __init__(self, magic, length, data):
        self.magic  = magic
        self.length = length
        self.data   = data

    @classmethod
    def create(cls, magic, data):
        return QcowHeaderExtension(magic, len(data), data)

class QcowHeader:

    uint32_t = 'I'
    uint64_t = 'Q'

    fields = [
        # Version 2 header fields
        [ uint32_t, '%#x',  'magic' ],
        [ uint32_t, '%d',   'version' ],
        [ uint64_t, '%#x',  'backing_file_offset' ],
        [ uint32_t, '%#x',  'backing_file_size' ],
        [ uint32_t, '%d',   'cluster_bits' ],
        [ uint64_t, '%d',   'size' ],
        [ uint32_t, '%d',   'crypt_method' ],
        [ uint32_t, '%d',   'l1_size' ],
        [ uint64_t, '%#x',  'l1_table_offset' ],
        [ uint64_t, '%#x',  'refcount_table_offset' ],
        [ uint32_t, '%d',   'refcount_table_clusters' ],
        [ uint32_t, '%d',   'nb_snapshots' ],
        [ uint64_t, '%#x',  'snapshot_offset' ],

        # Version 3 header fields
        [ uint64_t, '%#x',  'incompatible_features' ],
        [ uint64_t, '%#x',  'compatible_features' ],
        [ uint64_t, '%#x',  'autoclear_features' ],
        [ uint32_t, '%d',   'refcount_order' ],
        [ uint32_t, '%d',   'header_length' ],
    ];

    fmt = '>' + ''.join(field[0] for field in fields)

    def __init__(self, fd):

        buf_size = struct.calcsize(QcowHeader.fmt)

        fd.seek(0)
        buf = fd.read(buf_size)

        header = struct.unpack(QcowHeader.fmt, buf)
        self.__dict__ = dict((field[2], header[i])
            for i, field in enumerate(QcowHeader.fields))

        self.set_defaults()
        self.cluster_size = 1 << self.cluster_bits

        fd.seek(self.header_length)
        self.load_extensions(fd)

        if self.backing_file_offset:
            fd.seek(self.backing_file_offset)
            self.backing_file = fd.read(self.backing_file_size)
        else:
            self.backing_file = None

    def set_defaults(self):
        if self.version == 2:
            self.incompatible_features = 0
            self.compatible_features = 0
            self.autoclear_features = 0
            self.refcount_order = 4
            self.header_length = 72

    def load_extensions(self, fd):
        self.extensions = []

        if self.backing_file_offset != 0:
            end = min(self.cluster_size, self.backing_file_offset)
        else:
            end = self.cluster_size

        while fd.tell() < end:
            (magic, length) = struct.unpack('>II', fd.read(8))
            if magic == 0:
                break
            else:
                padded = (length + 7) & ~7
                data = fd.read(padded)
                self.extensions.append(QcowHeaderExtension(magic, length, data))

    def update_extensions(self, fd):

        fd.seek(self.header_length)
        extensions = self.extensions
        extensions.append(QcowHeaderExtension(0, 0, ""))
        for ex in extensions:
            buf = struct.pack('>II', ex.magic, ex.length)
            fd.write(buf)
            fd.write(ex.data)

        if self.backing_file != None:
            self.backing_file_offset = fd.tell()
            fd.write(self.backing_file)

        if fd.tell() > self.cluster_size:
            raise Exception("I think I just broke the image...")


    def update(self, fd):
        header_bytes = self.header_length

        self.update_extensions(fd)

        fd.seek(0)
        header = tuple(self.__dict__[f] for t, p, f in QcowHeader.fields)
        buf = struct.pack(QcowHeader.fmt, *header)
        buf = buf[0:header_bytes-1]
        fd.write(buf)

    def dump(self):
        for f in QcowHeader.fields:
            print "%-25s" % f[2], f[1] % self.__dict__[f[2]]
        print ""

    def dump_extensions(self):
        for ex in self.extensions:

            data = ex.data[:ex.length]
            if all(c in string.printable for c in data):
                data = "'%s'" % data
            else:
                data = "<binary>"

            print "Header extension:"
            print "%-25s %#x" % ("magic", ex.magic)
            print "%-25s %d" % ("length", ex.length)
            print "%-25s %s" % ("data", data)
            print ""


def cmd_dump_header(fd):
    h = QcowHeader(fd)
    h.dump()
    h.dump_extensions()

def cmd_add_header_ext(fd, magic, data):
    try:
        magic = int(magic, 0)
    except:
        print "'%s' is not a valid magic number" % magic
        sys.exit(1)

    h = QcowHeader(fd)
    h.extensions.append(QcowHeaderExtension.create(magic, data))
    h.update(fd)

def cmd_del_header_ext(fd, magic):
    try:
        magic = int(magic, 0)
    except:
        print "'%s' is not a valid magic number" % magic
        sys.exit(1)

    h = QcowHeader(fd)
    found = False

    for ex in h.extensions:
        if ex.magic == magic:
            found = True
            h.extensions.remove(ex)

    if not found:
        print "No such header extension"
        return

    h.update(fd)

def cmd_set_feature_bit(fd, group, bit):
    try:
        bit = int(bit, 0)
        if bit < 0 or bit >= 64:
            raise ValueError
    except:
        print "'%s' is not a valid bit number in range [0, 64)" % bit
        sys.exit(1)

    h = QcowHeader(fd)
    if group == 'incompatible':
        h.incompatible_features |= 1 << bit
    elif group == 'compatible':
        h.compatible_features |= 1 << bit
    elif group == 'autoclear':
        h.autoclear_features |= 1 << bit
    else:
        print "'%s' is not a valid group, try 'incompatible', 'compatible', or 'autoclear'" % group
        sys.exit(1)

    h.update(fd)

cmds = [
    [ 'dump-header',    cmd_dump_header,    0, 'Dump image header and header extensions' ],
    [ 'add-header-ext', cmd_add_header_ext, 2, 'Add a header extension' ],
    [ 'del-header-ext', cmd_del_header_ext, 1, 'Delete a header extension' ],
    [ 'set-feature-bit', cmd_set_feature_bit, 2, 'Set a feature bit'],
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
        print "Unknown command '%s'" % cmd
    finally:
        fd.close()

def usage():
    print "Usage: %s <file> <cmd> [<arg>, ...]" % sys.argv[0]
    print ""
    print "Supported commands:"
    for name, handler, num_args, desc in cmds:
        print "    %-20s - %s" % (name, desc)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        usage()
        sys.exit(1)

    main(sys.argv[1], sys.argv[2], sys.argv[3:])
