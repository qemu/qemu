# Generator of fuzzed qcow2 images
#
# Copyright (C) 2014 Maria Kustova <maria.k@catit.be>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
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

import random
import struct
import fuzz
from math import ceil
from os import urandom
from itertools import chain

MAX_IMAGE_SIZE = 10 * (1 << 20)
# Standard sizes
UINT32_S = 4
UINT64_S = 8


class Field(object):

    """Atomic image element (field).

    The class represents an image field as quadruple of a data format
    of value necessary for its packing to binary form, an offset from
    the beginning of the image, a value and a name.

    The field can be iterated as a list [format, offset, value, name].
    """

    __slots__ = ('fmt', 'offset', 'value', 'name')

    def __init__(self, fmt, offset, val, name):
        self.fmt = fmt
        self.offset = offset
        self.value = val
        self.name = name

    def __iter__(self):
        return iter([self.fmt, self.offset, self.value, self.name])

    def __repr__(self):
        return "Field(fmt='%s', offset=%d, value=%s, name=%s)" % \
            (self.fmt, self.offset, str(self.value), self.name)


class FieldsList(object):

    """List of fields.

    The class allows access to a field in the list by its name.
    """

    def __init__(self, meta_data=None):
        if meta_data is None:
            self.data = []
        else:
            self.data = [Field(*f)
                         for f in meta_data]

    def __getitem__(self, name):
        return [x for x in self.data if x.name == name]

    def __iter__(self):
        return iter(self.data)

    def __len__(self):
        return len(self.data)


class Image(object):

    """ Qcow2 image object.

    This class allows to create qcow2 images with random valid structures and
    values, fuzz them via external qcow2.fuzz module and write the result to
    a file.
    """

    def __init__(self, backing_file_name=None):
        """Create a random valid qcow2 image with the correct header and stored
        backing file name.
        """
        cluster_bits, self.image_size = self._size_params()
        self.cluster_size = 1 << cluster_bits
        self.header = FieldsList()
        self.backing_file_name = FieldsList()
        self.backing_file_format = FieldsList()
        self.feature_name_table = FieldsList()
        self.end_of_extension_area = FieldsList()
        self.l2_tables = FieldsList()
        self.l1_table = FieldsList()
        self.ext_offset = 0
        self.create_header(cluster_bits, backing_file_name)
        self.set_backing_file_name(backing_file_name)
        self.data_clusters = self._alloc_data(self.image_size,
                                              self.cluster_size)
        # Percentage of fields will be fuzzed
        self.bias = random.uniform(0.2, 0.5)

    def __iter__(self):
        return chain(self.header, self.backing_file_format,
                     self.feature_name_table, self.end_of_extension_area,
                     self.backing_file_name, self.l1_table, self.l2_tables)

    def create_header(self, cluster_bits, backing_file_name=None):
        """Generate a random valid header."""
        meta_header = [
            ['>4s', 0, "QFI\xfb", 'magic'],
            ['>I', 4, random.randint(2, 3), 'version'],
            ['>Q', 8, 0, 'backing_file_offset'],
            ['>I', 16, 0, 'backing_file_size'],
            ['>I', 20, cluster_bits, 'cluster_bits'],
            ['>Q', 24, self.image_size, 'size'],
            ['>I', 32, 0, 'crypt_method'],
            ['>I', 36, 0, 'l1_size'],
            ['>Q', 40, 0, 'l1_table_offset'],
            ['>Q', 48, 0, 'refcount_table_offset'],
            ['>I', 56, 0, 'refcount_table_clusters'],
            ['>I', 60, 0, 'nb_snapshots'],
            ['>Q', 64, 0, 'snapshots_offset'],
            ['>Q', 72, 0, 'incompatible_features'],
            ['>Q', 80, 0, 'compatible_features'],
            ['>Q', 88, 0, 'autoclear_features'],
            # Only refcount_order = 4 is supported by current (07.2014)
            # implementation of QEMU
            ['>I', 96, 4, 'refcount_order'],
            ['>I', 100, 0, 'header_length']
        ]
        self.header = FieldsList(meta_header)

        if self.header['version'][0].value == 2:
            self.header['header_length'][0].value = 72
        else:
            self.header['incompatible_features'][0].value = \
                                                        random.getrandbits(2)
            self.header['compatible_features'][0].value = random.getrandbits(1)
            self.header['header_length'][0].value = 104
        # Extensions start at the header last field offset and the field size
        self.ext_offset = struct.calcsize(
            self.header['header_length'][0].fmt) + \
            self.header['header_length'][0].offset
        end_of_extension_area_len = 2 * UINT32_S
        free_space = self.cluster_size - self.ext_offset - \
                     end_of_extension_area_len
        # If the backing file name specified and there is enough space for it
        # in the first cluster, then it's placed in the very end of the first
        # cluster.
        if (backing_file_name is not None) and \
           (free_space >= len(backing_file_name)):
            self.header['backing_file_size'][0].value = len(backing_file_name)
            self.header['backing_file_offset'][0].value = \
                                    self.cluster_size - len(backing_file_name)

    def set_backing_file_name(self, backing_file_name=None):
        """Add the name of the backing file at the offset specified
        in the header.
        """
        if (backing_file_name is not None) and \
           (not self.header['backing_file_offset'][0].value == 0):
            data_len = len(backing_file_name)
            data_fmt = '>' + str(data_len) + 's'
            self.backing_file_name = FieldsList([
                [data_fmt, self.header['backing_file_offset'][0].value,
                 backing_file_name, 'bf_name']
            ])

    def set_backing_file_format(self, backing_file_fmt=None):
        """Generate the header extension for the backing file format."""
        if backing_file_fmt is not None:
            # Calculation of the free space available in the first cluster
            end_of_extension_area_len = 2 * UINT32_S
            high_border = (self.header['backing_file_offset'][0].value or
                           (self.cluster_size - 1)) - \
                end_of_extension_area_len
            free_space = high_border - self.ext_offset
            ext_size = 2 * UINT32_S + ((len(backing_file_fmt) + 7) & ~7)

            if free_space >= ext_size:
                ext_data_len = len(backing_file_fmt)
                ext_data_fmt = '>' + str(ext_data_len) + 's'
                ext_padding_len = 7 - (ext_data_len - 1) % 8
                self.backing_file_format = FieldsList([
                    ['>I', self.ext_offset, 0xE2792ACA, 'ext_magic'],
                    ['>I', self.ext_offset + UINT32_S, ext_data_len,
                     'ext_length'],
                    [ext_data_fmt, self.ext_offset + UINT32_S * 2,
                     backing_file_fmt, 'bf_format']
                ])
                self.ext_offset = \
                        struct.calcsize(
                            self.backing_file_format['bf_format'][0].fmt) + \
                        ext_padding_len + \
                        self.backing_file_format['bf_format'][0].offset

    def create_feature_name_table(self):
        """Generate a random header extension for names of features used in
        the image.
        """
        def gen_feat_ids():
            """Return random feature type and feature bit."""
            return (random.randint(0, 2), random.randint(0, 63))

        end_of_extension_area_len = 2 * UINT32_S
        high_border = (self.header['backing_file_offset'][0].value or
                       (self.cluster_size - 1)) - \
            end_of_extension_area_len
        free_space = high_border - self.ext_offset
        # Sum of sizes of 'magic' and 'length' header extension fields
        ext_header_len = 2 * UINT32_S
        fnt_entry_size = 6 * UINT64_S
        num_fnt_entries = min(10, (free_space - ext_header_len) /
                              fnt_entry_size)
        if not num_fnt_entries == 0:
            feature_tables = []
            feature_ids = []
            inner_offset = self.ext_offset + ext_header_len
            feat_name = 'some cool feature'
            while len(feature_tables) < num_fnt_entries * 3:
                feat_type, feat_bit = gen_feat_ids()
                # Remove duplicates
                while (feat_type, feat_bit) in feature_ids:
                    feat_type, feat_bit = gen_feat_ids()
                feature_ids.append((feat_type, feat_bit))
                feat_fmt = '>' + str(len(feat_name)) + 's'
                feature_tables += [['B', inner_offset,
                                    feat_type, 'feature_type'],
                                   ['B', inner_offset + 1, feat_bit,
                                    'feature_bit_number'],
                                   [feat_fmt, inner_offset + 2,
                                    feat_name, 'feature_name']
                ]
                inner_offset += fnt_entry_size
            # No padding for the extension is necessary, because
            # the extension length is multiple of 8
            self.feature_name_table = FieldsList([
                ['>I', self.ext_offset, 0x6803f857, 'ext_magic'],
                # One feature table contains 3 fields and takes 48 bytes
                ['>I', self.ext_offset + UINT32_S,
                 len(feature_tables) / 3 * 48, 'ext_length']
            ] + feature_tables)
            self.ext_offset = inner_offset

    def set_end_of_extension_area(self):
        """Generate a mandatory header extension marking end of header
        extensions.
        """
        self.end_of_extension_area = FieldsList([
            ['>I', self.ext_offset, 0, 'ext_magic'],
            ['>I', self.ext_offset + UINT32_S, 0, 'ext_length']
        ])

    def create_l_structures(self):
        """Generate random valid L1 and L2 tables."""
        def create_l2_entry(host, guest, l2_cluster):
            """Generate one L2 entry."""
            offset = l2_cluster * self.cluster_size
            l2_size = self.cluster_size / UINT64_S
            entry_offset = offset + UINT64_S * (guest % l2_size)
            cluster_descriptor = host * self.cluster_size
            if not self.header['version'][0].value == 2:
                cluster_descriptor += random.randint(0, 1)
            # While snapshots are not supported, bit #63 = 1
            # Compressed clusters are not supported => bit #62 = 0
            entry_val = (1 << 63) + cluster_descriptor
            return ['>Q', entry_offset, entry_val, 'l2_entry']

        def create_l1_entry(l2_cluster, l1_offset, guest):
            """Generate one L1 entry."""
            l2_size = self.cluster_size / UINT64_S
            entry_offset = l1_offset + UINT64_S * (guest / l2_size)
            # While snapshots are not supported bit #63 = 1
            entry_val = (1 << 63) + l2_cluster * self.cluster_size
            return ['>Q', entry_offset, entry_val, 'l1_entry']

        if len(self.data_clusters) == 0:
            # All metadata for an empty guest image needs 4 clusters:
            # header, rfc table, rfc block, L1 table.
            # Header takes cluster #0, other clusters ##1-3 can be used
            l1_offset = random.randint(1, 3) * self.cluster_size
            l1 = [['>Q', l1_offset, 0, 'l1_entry']]
            l2 = []
        else:
            meta_data = self._get_metadata()
            guest_clusters = random.sample(range(self.image_size /
                                                 self.cluster_size),
                                           len(self.data_clusters))
            # Number of entries in a L1/L2 table
            l_size = self.cluster_size / UINT64_S
            # Number of clusters necessary for L1 table
            l1_size = int(ceil((max(guest_clusters) + 1) / float(l_size**2)))
            l1_start = self._get_adjacent_clusters(self.data_clusters |
                                                   meta_data, l1_size)
            meta_data |= set(range(l1_start, l1_start + l1_size))
            l1_offset = l1_start * self.cluster_size
            # Indices of L2 tables
            l2_ids = []
            # Host clusters allocated for L2 tables
            l2_clusters = []
            # L1 entries
            l1 = []
            # L2 entries
            l2 = []
            for host, guest in zip(self.data_clusters, guest_clusters):
                l2_id = guest / l_size
                if l2_id not in l2_ids:
                    l2_ids.append(l2_id)
                    l2_clusters.append(self._get_adjacent_clusters(
                        self.data_clusters | meta_data | set(l2_clusters),
                        1))
                    l1.append(create_l1_entry(l2_clusters[-1], l1_offset,
                                              guest))
                l2.append(create_l2_entry(host, guest,
                                          l2_clusters[l2_ids.index(l2_id)]))
        self.l2_tables = FieldsList(l2)
        self.l1_table = FieldsList(l1)
        self.header['l1_size'][0].value = int(ceil(UINT64_S * self.image_size /
                                                float(self.cluster_size**2)))
        self.header['l1_table_offset'][0].value = l1_offset

    def fuzz(self, fields_to_fuzz=None):
        """Fuzz an image by corrupting values of a random subset of its fields.

        Without parameters the method fuzzes an entire image.

        If 'fields_to_fuzz' is specified then only fields in this list will be
        fuzzed. 'fields_to_fuzz' can contain both individual fields and more
        general image elements as a header or tables.

        In the first case the field will be fuzzed always.
        In the second a random subset of fields will be selected and fuzzed.
        """
        def coin():
            """Return boolean value proportional to a portion of fields to be
            fuzzed.
            """
            return random.random() < self.bias

        if fields_to_fuzz is None:
            for field in self:
                if coin():
                    field.value = getattr(fuzz, field.name)(field.value)
        else:
            for item in fields_to_fuzz:
                if len(item) == 1:
                    for field in getattr(self, item[0]):
                        if coin():
                            field.value = getattr(fuzz,
                                                  field.name)(field.value)
                else:
                    # If fields with the requested name were not generated
                    # getattr(self, item[0])[item[1]] returns an empty list
                    for field in getattr(self, item[0])[item[1]]:
                        field.value = getattr(fuzz, field.name)(field.value)

    def write(self, filename):
        """Write an entire image to the file."""
        image_file = open(filename, 'w')
        for field in self:
            image_file.seek(field.offset)
            image_file.write(struct.pack(field.fmt, field.value))

        for cluster in sorted(self.data_clusters):
            image_file.seek(cluster * self.cluster_size)
            image_file.write(urandom(self.cluster_size))

        # Align the real image size to the cluster size
        image_file.seek(0, 2)
        size = image_file.tell()
        rounded = (size + self.cluster_size - 1) & ~(self.cluster_size - 1)
        if rounded > size:
            image_file.seek(rounded - 1)
            image_file.write("\0")
        image_file.close()

    @staticmethod
    def _size_params():
        """Generate a random image size aligned to a random correct
        cluster size.
        """
        cluster_bits = random.randrange(9, 21)
        cluster_size = 1 << cluster_bits
        img_size = random.randrange(0, MAX_IMAGE_SIZE + 1, cluster_size)
        return (cluster_bits, img_size)

    @staticmethod
    def _get_available_clusters(used, number):
        """Return a set of indices of not allocated clusters.

        'used' contains indices of currently allocated clusters.
        All clusters that cannot be allocated between 'used' clusters will have
        indices appended to the end of 'used'.
        """
        append_id = max(used) + 1
        free = set(range(1, append_id)) - used
        if len(free) >= number:
            return set(random.sample(free, number))
        else:
            return free | set(range(append_id, append_id + number - len(free)))

    @staticmethod
    def _get_adjacent_clusters(used, size):
        """Return an index of the first cluster in the sequence of free ones.

        'used' contains indices of currently allocated clusters. 'size' is the
        length of the sequence of free clusters.
        If the sequence of 'size' is not available between 'used' clusters, its
        first index will be append to the end of 'used'.
        """
        def get_cluster_id(lst, length):
            """Return the first index of the sequence of the specified length
            or None if the sequence cannot be inserted in the list.
            """
            if len(lst) != 0:
                pairs = []
                pair = (lst[0], 1)
                for i in range(1, len(lst)):
                    if lst[i] == lst[i-1] + 1:
                        pair = (lst[i], pair[1] + 1)
                    else:
                        pairs.append(pair)
                        pair = (lst[i], 1)
                pairs.append(pair)
                random.shuffle(pairs)
                for x, s in pairs:
                    if s >= length:
                        return x - length + 1
            return None

        append_id = max(used) + 1
        free = list(set(range(1, append_id)) - used)
        idx = get_cluster_id(free, size)
        if idx is None:
            return append_id
        else:
            return idx

    @staticmethod
    def _alloc_data(img_size, cluster_size):
        """Return a set of random indices of clusters allocated for guest data.
        """
        num_of_cls = img_size/cluster_size
        return set(random.sample(range(1, num_of_cls + 1),
                                 random.randint(0, num_of_cls)))

    def _get_metadata(self):
        """Return indices of clusters allocated for image metadata."""
        ids = set()
        for x in self:
            ids.add(x.offset/self.cluster_size)
        return ids


def create_image(test_img_path, backing_file_name=None, backing_file_fmt=None,
                 fields_to_fuzz=None):
    """Create a fuzzed image and write it to the specified file."""
    image = Image(backing_file_name)
    image.set_backing_file_format(backing_file_fmt)
    image.create_feature_name_table()
    image.set_end_of_extension_area()
    image.create_l_structures()
    image.fuzz(fields_to_fuzz)
    image.write(test_img_path)
    return image.image_size
