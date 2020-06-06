# Library for manipulations with qcow2 image
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

import struct
import string


class QcowHeaderExtension:

    def __init__(self, magic, length, data):
        if length % 8 != 0:
            padding = 8 - (length % 8)
            data += b"\0" * padding

        self.magic = magic
        self.length = length
        self.data = data

    @classmethod
    def create(cls, magic, data):
        return QcowHeaderExtension(magic, len(data), data)


class QcowHeader:

    uint32_t = 'I'
    uint64_t = 'Q'

    fields = [
        # Version 2 header fields
        [uint32_t, '%#x',  'magic'],
        [uint32_t, '%d',   'version'],
        [uint64_t, '%#x',  'backing_file_offset'],
        [uint32_t, '%#x',  'backing_file_size'],
        [uint32_t, '%d',   'cluster_bits'],
        [uint64_t, '%d',   'size'],
        [uint32_t, '%d',   'crypt_method'],
        [uint32_t, '%d',   'l1_size'],
        [uint64_t, '%#x',  'l1_table_offset'],
        [uint64_t, '%#x',  'refcount_table_offset'],
        [uint32_t, '%d',   'refcount_table_clusters'],
        [uint32_t, '%d',   'nb_snapshots'],
        [uint64_t, '%#x',  'snapshot_offset'],

        # Version 3 header fields
        [uint64_t, 'mask', 'incompatible_features'],
        [uint64_t, 'mask', 'compatible_features'],
        [uint64_t, 'mask', 'autoclear_features'],
        [uint32_t, '%d',   'refcount_order'],
        [uint32_t, '%d',   'header_length'],
    ]

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
                self.extensions.append(QcowHeaderExtension(magic, length,
                                                           data))

    def update_extensions(self, fd):

        fd.seek(self.header_length)
        extensions = self.extensions
        extensions.append(QcowHeaderExtension(0, 0, b""))
        for ex in extensions:
            buf = struct.pack('>II', ex.magic, ex.length)
            fd.write(buf)
            fd.write(ex.data)

        if self.backing_file is not None:
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
            value = self.__dict__[f[2]]
            if f[1] == 'mask':
                bits = []
                for bit in range(64):
                    if value & (1 << bit):
                        bits.append(bit)
                value_str = str(bits)
            else:
                value_str = f[1] % value

            print("%-25s" % f[2], value_str)
        print("")

    def dump_extensions(self):
        for ex in self.extensions:

            data = ex.data[:ex.length]
            if all(c in string.printable.encode('ascii') for c in data):
                data = "'%s'" % data.decode('ascii')
            else:
                data = "<binary>"

            print("Header extension:")
            print("%-25s %#x" % ("magic", ex.magic))
            print("%-25s %d" % ("length", ex.length))
            print("%-25s %s" % ("data", data))
            print("")
