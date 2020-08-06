# Library for manipulations with qcow2 image
#
# Copyright (c) 2020 Virtuozzo International GmbH.
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

import struct
import string
import json


class ComplexEncoder(json.JSONEncoder):
    def default(self, obj):
        if hasattr(obj, 'to_json'):
            return obj.to_json()
        else:
            return json.JSONEncoder.default(self, obj)


class Qcow2Field:

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return str(self.value)


class Flags64(Qcow2Field):

    def __str__(self):
        bits = []
        for bit in range(64):
            if self.value & (1 << bit):
                bits.append(bit)
        return str(bits)


class BitmapFlags(Qcow2Field):

    flags = {
        0x1: 'in-use',
        0x2: 'auto'
    }

    def __str__(self):
        bits = []
        for bit in range(64):
            flag = self.value & (1 << bit)
            if flag:
                bits.append(self.flags.get(flag, f'bit-{bit}'))
        return f'{self.value:#x} ({bits})'


class Enum(Qcow2Field):

    def __str__(self):
        return f'{self.value:#x} ({self.mapping.get(self.value, "<unknown>")})'


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

    def dump(self, is_json=False):
        if is_json:
            print(json.dumps(self.to_json(), indent=4, cls=ComplexEncoder))
            return

        for f in self.fields:
            value = self.__dict__[f[2]]
            if isinstance(f[1], str):
                value_str = f[1].format(value)
            else:
                value_str = str(f[1](value))

            print('{:<25} {}'.format(f[2], value_str))

    def to_json(self):
        return dict((f[2], self.__dict__[f[2]]) for f in self.fields)


class Qcow2BitmapExt(Qcow2Struct):

    fields = (
        ('u32', '{}', 'nb_bitmaps'),
        ('u32', '{}', 'reserved32'),
        ('u64', '{:#x}', 'bitmap_directory_size'),
        ('u64', '{:#x}', 'bitmap_directory_offset')
    )

    def __init__(self, fd, cluster_size):
        super().__init__(fd=fd)
        tail = struct.calcsize(self.fmt) % 8
        if tail:
            fd.seek(8 - tail, 1)
        position = fd.tell()
        self.cluster_size = cluster_size
        self.read_bitmap_directory(fd)
        fd.seek(position)

    def read_bitmap_directory(self, fd):
        fd.seek(self.bitmap_directory_offset)
        self.bitmap_directory = \
            [Qcow2BitmapDirEntry(fd, cluster_size=self.cluster_size)
             for _ in range(self.nb_bitmaps)]

    def dump(self):
        super().dump()
        for entry in self.bitmap_directory:
            print()
            entry.dump()

    def to_json(self):
        fields_dict = super().to_json()
        fields_dict['bitmap_directory'] = self.bitmap_directory
        return fields_dict


class Qcow2BitmapDirEntry(Qcow2Struct):

    fields = (
        ('u64', '{:#x}', 'bitmap_table_offset'),
        ('u32', '{}', 'bitmap_table_size'),
        ('u32', BitmapFlags, 'flags'),
        ('u8',  '{}', 'type'),
        ('u8',  '{}', 'granularity_bits'),
        ('u16', '{}', 'name_size'),
        ('u32', '{}', 'extra_data_size')
    )

    def __init__(self, fd, cluster_size):
        super().__init__(fd=fd)
        self.cluster_size = cluster_size
        # Seek relative to the current position in the file
        fd.seek(self.extra_data_size, 1)
        bitmap_name = fd.read(self.name_size)
        self.name = bitmap_name.decode('ascii')
        # Move position to the end of the entry in the directory
        entry_raw_size = self.bitmap_dir_entry_raw_size()
        padding = ((entry_raw_size + 7) & ~7) - entry_raw_size
        fd.seek(padding, 1)
        self.bitmap_table = Qcow2BitmapTable(fd=fd,
                                             offset=self.bitmap_table_offset,
                                             nb_entries=self.bitmap_table_size,
                                             cluster_size=self.cluster_size)

    def bitmap_dir_entry_raw_size(self):
        return struct.calcsize(self.fmt) + self.name_size + \
            self.extra_data_size

    def dump(self):
        print(f'{"Bitmap name":<25} {self.name}')
        super(Qcow2BitmapDirEntry, self).dump()
        self.bitmap_table.dump()

    def to_json(self):
        # Put the name ahead of the dict
        return {
            'name': self.name,
            **super().to_json(),
            'bitmap_table': self.bitmap_table
        }


class Qcow2BitmapTableEntry(Qcow2Struct):

    fields = (
        ('u64',  '{}', 'entry'),
    )

    BME_TABLE_ENTRY_RESERVED_MASK = 0xff000000000001fe
    BME_TABLE_ENTRY_OFFSET_MASK = 0x00fffffffffffe00
    BME_TABLE_ENTRY_FLAG_ALL_ONES = 1

    def __init__(self, fd):
        super().__init__(fd=fd)
        self.reserved = self.entry & self.BME_TABLE_ENTRY_RESERVED_MASK
        self.offset = self.entry & self.BME_TABLE_ENTRY_OFFSET_MASK
        if self.offset:
            if self.entry & self.BME_TABLE_ENTRY_FLAG_ALL_ONES:
                self.type = 'invalid'
            else:
                self.type = 'serialized'
        elif self.entry & self.BME_TABLE_ENTRY_FLAG_ALL_ONES:
            self.type = 'all-ones'
        else:
            self.type = 'all-zeroes'

    def to_json(self):
        return {'type': self.type, 'offset': self.offset,
                'reserved': self.reserved}


class Qcow2BitmapTable:

    def __init__(self, fd, offset, nb_entries, cluster_size):
        self.cluster_size = cluster_size
        position = fd.tell()
        fd.seek(offset)
        self.entries = [Qcow2BitmapTableEntry(fd) for _ in range(nb_entries)]
        fd.seek(position)

    def dump(self):
        bitmap_table = enumerate(self.entries)
        print(f'{"Bitmap table":<14} {"type":<15} {"size":<12} {"offset"}')
        for i, entry in bitmap_table:
            if entry.type == 'serialized':
                size = self.cluster_size
            else:
                size = 0
            print(f'{i:<14} {entry.type:<15} {size:<12} {entry.offset}')

    def to_json(self):
        return self.entries


QCOW2_EXT_MAGIC_BITMAPS = 0x23852875


class QcowHeaderExtension(Qcow2Struct):

    class Magic(Enum):
        mapping = {
            0xe2792aca: 'Backing format',
            0x6803f857: 'Feature table',
            0x0537be77: 'Crypto header',
            QCOW2_EXT_MAGIC_BITMAPS: 'Bitmaps',
            0x44415441: 'Data file'
        }

        def to_json(self):
            return self.mapping.get(self.value, "<unknown>")

    fields = (
        ('u32', Magic, 'magic'),
        ('u32', '{}', 'length')
        # length bytes of data follows
        # then padding to next multiply of 8
    )

    def __init__(self, magic=None, length=None, data=None, fd=None,
                 cluster_size=None):
        """
        Support both loading from fd and creation from user data.
        For fd-based creation current position in a file will be used to read
        the data.
        The cluster_size value may be obtained by dependent structures.

        This should be somehow refactored and functionality should be moved to
        superclass (to allow creation of any qcow2 struct), but then, fields
        of variable length (data here) should be supported in base class
        somehow. Note also, that we probably want to parse different
        extensions. Should they be subclasses of this class, or how to do it
        better? Should it be something like QAPI union with discriminator field
        (magic here). So, it's a TODO. We'll see how to properly refactor this
        when we have more qcow2 structures.
        """
        if fd is None:
            assert all(v is not None for v in (magic, length, data))
            self.magic = magic
            self.length = length
            if length % 8 != 0:
                padding = 8 - (length % 8)
                data += b'\0' * padding
            self.data = data
        else:
            assert all(v is None for v in (magic, length, data))
            super().__init__(fd=fd)
            if self.magic == QCOW2_EXT_MAGIC_BITMAPS:
                self.obj = Qcow2BitmapExt(fd=fd, cluster_size=cluster_size)
                self.data = None
            else:
                padded = (self.length + 7) & ~7
                self.data = fd.read(padded)
                assert self.data is not None
                self.obj = None

        if self.data is not None:
            data_str = self.data[:self.length]
            if all(c in string.printable.encode(
                'ascii') for c in data_str):
                data_str = f"'{ data_str.decode('ascii') }'"
            else:
                data_str = '<binary>'
            self.data_str = data_str


    def dump(self):
        super().dump()

        if self.obj is None:
            print(f'{"data":<25} {self.data_str}')
        else:
            self.obj.dump()

    def to_json(self):
        # Put the name ahead of the dict
        res = {'name': self.Magic(self.magic), **super().to_json()}
        if self.obj is not None:
            res['data'] = self.obj
        else:
            res['data_str'] = self.data_str

        return res

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
        ('u64', Flags64, 'incompatible_features'),
        ('u64', Flags64, 'compatible_features'),
        ('u64', Flags64, 'autoclear_features'),
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
            ext = QcowHeaderExtension(fd=fd, cluster_size=self.cluster_size)
            if ext.magic == 0:
                break
            else:
                self.extensions.append(ext)

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

    def dump_extensions(self, is_json=False):
        if is_json:
            print(json.dumps(self.extensions, indent=4, cls=ComplexEncoder))
            return

        for ex in self.extensions:
            print('Header extension:')
            ex.dump()
            print()
