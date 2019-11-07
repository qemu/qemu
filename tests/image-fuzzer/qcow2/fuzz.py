# Fuzzing functions for qcow2 fields
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
from functools import reduce

UINT8 = 0xff
UINT16 = 0xffff
UINT32 = 0xffffffff
UINT64 = 0xffffffffffffffff
# Most significant bit orders
UINT32_M = 31
UINT64_M = 63
# Fuzz vectors
UINT8_V = [0, 0x10, UINT8//4, UINT8//2 - 1, UINT8//2, UINT8//2 + 1, UINT8 - 1,
           UINT8]
UINT16_V = [0, 0x100, 0x1000, UINT16//4, UINT16//2 - 1, UINT16//2, UINT16//2 + 1,
            UINT16 - 1, UINT16]
UINT32_V = [0, 0x100, 0x1000, 0x10000, 0x100000, UINT32//4, UINT32//2 - 1,
            UINT32//2, UINT32//2 + 1, UINT32 - 1, UINT32]
UINT64_V = UINT32_V + [0x1000000, 0x10000000, 0x100000000, UINT64//4,
                       UINT64//2 - 1, UINT64//2, UINT64//2 + 1, UINT64 - 1,
                       UINT64]
BYTES_V = [b'%s%p%x%d', b'.1024d', b'%.2049d', b'%p%p%p%p', b'%x%x%x%x',
           b'%d%d%d%d', b'%s%s%s%s', b'%99999999999s', b'%08x', b'%%20d', b'%%20n',
           b'%%20x', b'%%20s', b'%s%s%s%s%s%s%s%s%s%s', b'%p%p%p%p%p%p%p%p%p%p',
           b'%#0123456x%08x%x%s%p%d%n%o%u%c%h%l%q%j%z%Z%t%i%e%g%f%a%C%S%08x%%',
           b'%s x 129', b'%x x 257']


def random_from_intervals(intervals):
    """Select a random integer number from the list of specified intervals.

    Each interval is a tuple of lower and upper limits of the interval. The
    limits are included. Intervals in a list should not overlap.
    """
    total = reduce(lambda x, y: x + y[1] - y[0] + 1, intervals, 0)
    r = random.randint(0, total - 1) + intervals[0][0]
    for x in zip(intervals, intervals[1:]):
        r = r + (r > x[0][1]) * (x[1][0] - x[0][1] - 1)
    return r


def random_bits(bit_ranges):
    """Generate random binary mask with ones in the specified bit ranges.

    Each bit_ranges is a list of tuples of lower and upper limits of bit
    positions will be fuzzed. The limits are included. Random amount of bits
    in range limits will be set to ones. The mask is returned in decimal
    integer format.
    """
    bit_numbers = []
    # Select random amount of random positions in bit_ranges
    for rng in bit_ranges:
        bit_numbers += random.sample(range(rng[0], rng[1] + 1),
                                     random.randint(0, rng[1] - rng[0] + 1))
    val = 0
    # Set bits on selected positions to ones
    for bit in bit_numbers:
        val |= 1 << bit
    return val


def truncate_bytes(sequences, length):
    """Return sequences truncated to specified length."""
    if type(sequences) == list:
        return [s[:length] for s in sequences]
    else:
        return sequences[:length]


def validator(current, pick, choices):
    """Return a value not equal to the current selected by the pick
    function from choices.
    """
    while True:
        val = pick(choices)
        if not val == current:
            return val


def int_validator(current, intervals):
    """Return a random value from intervals not equal to the current.

    This function is useful for selection from valid values except current one.
    """
    return validator(current, random_from_intervals, intervals)


def bit_validator(current, bit_ranges):
    """Return a random bit mask not equal to the current.

    This function is useful for selection from valid values except current one.
    """
    return validator(current, random_bits, bit_ranges)


def bytes_validator(current, sequences):
    """Return a random bytes value from the list not equal to the current.

    This function is useful for selection from valid values except current one.
    """
    return validator(current, random.choice, sequences)


def selector(current, constraints, validate=int_validator):
    """Select one value from all defined by constraints.

    Each constraint produces one random value satisfying to it. The function
    randomly selects one value satisfying at least one constraint (depending on
    constraints overlaps).
    """
    def iter_validate(c):
        """Apply validate() only to constraints represented as lists.

        This auxiliary function replaces short circuit conditions not supported
        in Python 2.4
        """
        if type(c) == list:
            return validate(current, c)
        else:
            return c

    fuzz_values = [iter_validate(c) for c in constraints]
    # Remove current for cases it's implicitly specified in constraints
    # Duplicate validator functionality to prevent decreasing of probability
    # to get one of allowable values
    # TODO: remove validators after implementation of intelligent selection
    # of fields will be fuzzed
    try:
        fuzz_values.remove(current)
    except ValueError:
        pass
    return random.choice(fuzz_values)


def magic(current):
    """Fuzz magic header field.

    The function just returns the current magic value and provides uniformity
    of calls for all fuzzing functions.
    """
    return current


def version(current):
    """Fuzz version header field."""
    constraints = UINT32_V + [
        [(2, 3)],  # correct values
        [(0, 1), (4, UINT32)]
    ]
    return selector(current, constraints)


def backing_file_offset(current):
    """Fuzz backing file offset header field."""
    constraints = UINT64_V
    return selector(current, constraints)


def backing_file_size(current):
    """Fuzz backing file size header field."""
    constraints = UINT32_V
    return selector(current, constraints)


def cluster_bits(current):
    """Fuzz cluster bits header field."""
    constraints = UINT32_V + [
        [(9, 20)],  # correct values
        [(0, 9), (20, UINT32)]
    ]
    return selector(current, constraints)


def size(current):
    """Fuzz image size header field."""
    constraints = UINT64_V
    return selector(current, constraints)


def crypt_method(current):
    """Fuzz crypt method header field."""
    constraints = UINT32_V + [
        1,
        [(2, UINT32)]
    ]
    return selector(current, constraints)


def l1_size(current):
    """Fuzz L1 table size header field."""
    constraints = UINT32_V
    return selector(current, constraints)


def l1_table_offset(current):
    """Fuzz L1 table offset header field."""
    constraints = UINT64_V
    return selector(current, constraints)


def refcount_table_offset(current):
    """Fuzz refcount table offset header field."""
    constraints = UINT64_V
    return selector(current, constraints)


def refcount_table_clusters(current):
    """Fuzz refcount table clusters header field."""
    constraints = UINT32_V
    return selector(current, constraints)


def nb_snapshots(current):
    """Fuzz number of snapshots header field."""
    constraints = UINT32_V
    return selector(current, constraints)


def snapshots_offset(current):
    """Fuzz snapshots offset header field."""
    constraints = UINT64_V
    return selector(current, constraints)


def incompatible_features(current):
    """Fuzz incompatible features header field."""
    constraints = [
        [(0, 1)],  # allowable values
        [(0, UINT64_M)]
    ]
    return selector(current, constraints, bit_validator)


def compatible_features(current):
    """Fuzz compatible features header field."""
    constraints = [
        [(0, UINT64_M)]
    ]
    return selector(current, constraints, bit_validator)


def autoclear_features(current):
    """Fuzz autoclear features header field."""
    constraints = [
        [(0, UINT64_M)]
    ]
    return selector(current, constraints, bit_validator)


def refcount_order(current):
    """Fuzz number of refcount order header field."""
    constraints = UINT32_V
    return selector(current, constraints)


def header_length(current):
    """Fuzz number of refcount order header field."""
    constraints = UINT32_V + [
        72,
        104,
        [(0, UINT32)]
    ]
    return selector(current, constraints)


def bf_name(current):
    """Fuzz the backing file name."""
    constraints = [
        truncate_bytes(BYTES_V, len(current))
    ]
    return selector(current, constraints, bytes_validator)


def ext_magic(current):
    """Fuzz magic field of a header extension."""
    constraints = UINT32_V
    return selector(current, constraints)


def ext_length(current):
    """Fuzz length field of a header extension."""
    constraints = UINT32_V
    return selector(current, constraints)


def bf_format(current):
    """Fuzz backing file format in the corresponding header extension."""
    constraints = [
        truncate_bytes(BYTES_V, len(current)),
        truncate_bytes(BYTES_V, (len(current) + 7) & ~7)  # Fuzz padding
    ]
    return selector(current, constraints, bytes_validator)


def feature_type(current):
    """Fuzz feature type field of a feature name table header extension."""
    constraints = UINT8_V
    return selector(current, constraints)


def feature_bit_number(current):
    """Fuzz bit number field of a feature name table header extension."""
    constraints = UINT8_V
    return selector(current, constraints)


def feature_name(current):
    """Fuzz feature name field of a feature name table header extension."""
    constraints = [
        truncate_bytes(BYTES_V, len(current)),
        truncate_bytes(BYTES_V, 46)  # Fuzz padding (field length = 46)
    ]
    return selector(current, constraints, bytes_validator)


def l1_entry(current):
    """Fuzz an entry of the L1 table."""
    constraints = UINT64_V
    # Reserved bits are ignored
    # Added a possibility when only flags are fuzzed
    offset = 0x7fffffffffffffff & \
             random.choice([selector(current, constraints), current])
    is_cow = random.randint(0, 1)
    return offset + (is_cow << UINT64_M)


def l2_entry(current):
    """Fuzz an entry of an L2 table."""
    constraints = UINT64_V
    # Reserved bits are ignored
    # Add a possibility when only flags are fuzzed
    offset = 0x3ffffffffffffffe & \
             random.choice([selector(current, constraints), current])
    is_compressed = random.randint(0, 1)
    is_cow = random.randint(0, 1)
    is_zero = random.randint(0, 1)
    value = offset + (is_cow << UINT64_M) + \
            (is_compressed << UINT64_M - 1) + is_zero
    return value


def refcount_table_entry(current):
    """Fuzz an entry of the refcount table."""
    constraints = UINT64_V
    return selector(current, constraints)


def refcount_block_entry(current):
    """Fuzz an entry of a refcount block."""
    constraints = UINT16_V
    return selector(current, constraints)
