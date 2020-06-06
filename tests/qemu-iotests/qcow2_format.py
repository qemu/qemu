# Library for manipulations with qcow2 image
#
# Copyright (c) 2020 Virtuozzo International GmbH.
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


class Qcow2StructMeta(type):

    # Mapping from c types to python struct format
    ctypes = {
        'u8': 'B',
        'u16': 'H',
        'u32': 'I',
        'u64': 'Q'
    }

    def __init__(self, name, bases, attrs):
        if 'fields' in attrs:
            self.fmt = '>' + ''.join(self.ctypes[f[0]] for f in self.fields)


class Qcow2Struct(metaclass=Qcow2StructMeta):

    """Qcow2Struct: base class for qcow2 data structures

    Successors should define fields class variable, which is: list of tuples,
    each of three elements:
        - c-type (one of 'u8', 'u16', 'u32', 'u64')
        - format (format_spec to use with .format() when dump or 'mask' to dump
                  bitmasks)
        - field name
    """

    def __init__(self, fd=None, offset=None, data=None):
        """
        Two variants:
            1. Specify data. fd and offset must be None.
            2. Specify fd and offset, data must be None. offset may be omitted
               in this case, than current position of fd is used.
        """
        if data is None:
            assert fd is not None
            buf_size = struct.calcsize(self.fmt)
            if offset is not None:
                fd.seek(offset)
            data = fd.read(buf_size)
        else:
            assert fd is None and offset is None

        values = struct.unpack(self.fmt, data)
        self.__dict__ = dict((field[2], values[i])
                             for i, field in enumerate(self.fields))

    def dump(self):
        for f in self.fields:
            value = self.__dict__[f[2]]
            if f[1] == 'mask':
                bits = []
                for bit in range(64):
                    if value & (1 << bit):
                        bits.append(bit)
                value_str = str(bits)
            else:
                value_str = f[1].format(value)

            print('{:<25} {}'.format(f[2], value_str))


class QcowHeaderExtension:

    def __init__(self, magic, length, data):
        if length % 8 != 0:
            padding = 8 - (length % 8)
            data += b'\0' * padding

        self.magic = magic
        self.length = length
        self.data = data

    @classmethod
    def create(cls, magic, data):
        return QcowHeaderExtension(magic, len(data), data)


class QcowHeader(Qcow2Struct):

    fields = (
        # Version 2 header fields
        ('u32', '{:#x}', 'magic'),
        ('u32', '{}', 'version'),
        ('u64', '{:#x}', 'backing_file_offset'),
        ('u32', '{:#x}', 'backing_file_size'),
        ('u32', '{}', 'cluster_bits'),
        ('u64', '{}', 'size'),
        ('u32', '{}', 'crypt_method'),
        ('u32', '{}', 'l1_size'),
        ('u64', '{:#x}', 'l1_table_offset'),
        ('u64', '{:#x}', 'refcount_table_offset'),
        ('u32', '{}', 'refcount_table_clusters'),
        ('u32', '{}', 'nb_snapshots'),
        ('u64', '{:#x}', 'snapshot_offset'),

        # Version 3 header fields
        ('u64', 'mask', 'incompatible_features'),
        ('u64', 'mask', 'compatible_features'),
        ('u64', 'mask', 'autoclear_features'),
        ('u32', '{}', 'refcount_order'),
        ('u32', '{}', 'header_length'),
    )

    def __init__(self, fd):
        super().__init__(fd=fd, offset=0)

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
        extensions.append(QcowHeaderExtension(0, 0, b''))
        for ex in extensions:
            buf = struct.pack('>II', ex.magic, ex.length)
            fd.write(buf)
            fd.write(ex.data)

        if self.backing_file is not None:
            self.backing_file_offset = fd.tell()
            fd.write(self.backing_file)

        if fd.tell() > self.cluster_size:
            raise Exception('I think I just broke the image...')

    def update(self, fd):
        header_bytes = self.header_length

        self.update_extensions(fd)

        fd.seek(0)
        header = tuple(self.__dict__[f] for t, p, f in QcowHeader.fields)
        buf = struct.pack(QcowHeader.fmt, *header)
        buf = buf[0:header_bytes-1]
        fd.write(buf)

    def dump_extensions(self):
        for ex in self.extensions:

            data = ex.data[:ex.length]
            if all(c in string.printable.encode('ascii') for c in data):
                data = f"'{ data.decode('ascii') }'"
            else:
                data = '<binary>'

            print('Header extension:')
            print(f'{"magic":<25} {ex.magic:#x}')
            print(f'{"length":<25} {ex.length}')
            print(f'{"data":<25} {data}')
            print()
