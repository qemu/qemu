Parallels Expandable Image File Format
======================================

..
   Copyright (c) 2015 Denis Lunev
   Copyright (c) 2015 Vladimir Sementsov-Ogievskiy

   This work is licensed under the terms of the GNU GPL, version 2 or later.
   See the COPYING file in the top-level directory.


A Parallels expandable image file consists of three consecutive parts:

* header
* BAT
* data area

All numbers in a Parallels expandable image are stored in little-endian byte
order.


Definitions
-----------

Sector
  A 512-byte data chunk.

Cluster
  A data chunk of the size specified in the image header.
  Currently, the default size is 1MiB (2048 sectors). In previous
  versions, cluster sizes of 63 sectors, 256 and 252 kilobytes were used.

BAT
  Block Allocation Table, an entity that contains information for
  guest-to-host I/O data address translation.

Header
------

The header is placed at the start of an image and contains the following
fields::

 Bytes:
   0 - 15:    magic
              Must contain "WithoutFreeSpace" or "WithouFreSpacExt".

  16 - 19:    version
              Must be 2.

  20 - 23:    heads
              Disk geometry parameter for guest.

  24 - 27:    cylinders
              Disk geometry parameter for guest.

  28 - 31:    tracks
              Cluster size, in sectors.

  32 - 35:    nb_bat_entries
              Disk size, in clusters (BAT size).

  36 - 43:    nb_sectors
              Disk size, in sectors.

              For "WithoutFreeSpace" images:
              Only the lowest 4 bytes are used. The highest 4 bytes must be
              cleared in this case.

              For "WithouFreSpacExt" images, there are no such
              restrictions.

  44 - 47:    in_use
              Set to 0x746F6E59 when the image is opened by software in R/W
              mode; set to 0x312e3276 when the image is closed.

              A zero in this field means that the image was opened by an old
              version of the software that doesn't support Format Extension
              (see below).

              Other values are not allowed.

  48 - 51:    data_off
              An offset, in sectors, from the start of the file to the start of
              the data area.

              For "WithoutFreeSpace" images:
              - If data_off is zero, the offset is calculated as the end of BAT
                table plus some padding to ensure sector size alignment.
              - If data_off is non-zero, the offset should be aligned to sector
                size. However it is recommended to align it to cluster size for
                newly created images.

              For "WithouFreSpacExt" images:
              data_off must be non-zero and aligned to cluster size.

  52 - 55:    flags
              Miscellaneous flags.

              Bit 0: Empty Image bit. If set, the image should be
                     considered clear.

              Bits 1-31: Unused.

  56 - 63:    ext_off
              Format Extension offset, an offset, in sectors, from the start of
              the file to the start of the Format Extension Cluster.

              ext_off must meet the same requirements as cluster offsets
              defined by BAT entries (see below).

BAT
---

BAT is placed immediately after the image header. In the file, BAT is a
contiguous array of 32-bit unsigned little-endian integers with
``(bat_entries * 4)`` bytes size.

Each BAT entry contains an offset from the start of the file to the
corresponding cluster. The offset set in clusters for ``WithouFreSpacExt``
images and in sectors for ``WithoutFreeSpace`` images.

If a BAT entry is zero, the corresponding cluster is not allocated and should
be considered as filled with zeroes.

Cluster offsets specified by BAT entries must meet the following requirements:

- the value must not be lower than data offset (provided by ``header.data_off``
  or calculated as specified above)
- the value must be lower than the desired file size
- the value must be unique among all BAT entries
- the result of ``(cluster offset - data offset)`` must be aligned to
  cluster size

Data Area
---------

The data area is an area from the data offset (provided by ``header.data_off``
or calculated as specified above) to the end of the file. It represents a
contiguous array of clusters. Most of them are allocated by the BAT, some may
be allocated by the ``ext_off`` field in the header while other may be
allocated by extensions. All clusters allocated by ``ext_off`` and extensions
should meet the same requirements as clusters specified by BAT entries.


Format Extension
----------------

The Format Extension is an area 1 cluster in size that provides additional
format features. This cluster is addressed by the ext_off field in the header.
The format of the Format Extension area is the following::

   0 -  7:    magic
              Must be 0xAB234CEF23DCEA87

   8 - 23:    m_CheckSum
              The MD5 checksum of the entire Header Extension cluster except
              the first 24 bytes.

The above are followed by feature sections or "extensions". The last
extension must be "End of features" (see below).

Each feature section has the following format::

   0 -  7:    magic
              The identifier of the feature:
              0x0000000000000000 - End of features
              0x20385FAE252CB34A - Dirty bitmap

   8 - 15:    flags
              External flags for extension:

              Bit 0: NECESSARY
                     If the software cannot load the extension (due to an
                     unknown magic number or error), the file should not be
                     changed. If this flag is unset and there is an error on
                     loading the extension, said extension should be dropped.

              Bit 1: TRANSIT
                     If there is an unknown extension with this flag set,
                     said extension should be left as is.

              If neither NECESSARY nor TRANSIT are set, the extension should be
              dropped.

  16 - 19:    data_size
              The size of the following feature data, in bytes.

  20 - 23:    unused32
              Align header to 8 bytes boundary.

  variable:   data (data_size bytes)

The above is followed by padding to the next 8 bytes boundary, then the
next extension starts.

The last extension must be "End of features" with all the fields set to 0.


Dirty bitmaps feature
---------------------

This feature provides a way of storing dirty bitmaps in the image. The fields
of its data area are::

   0 -  7:    size
              The bitmap size, should be equal to disk size in sectors.

   8 - 23:    id
              An identifier for backup consistency checking.

  24 - 27:    granularity
              Bitmap granularity, in sectors. I.e., the number of sectors
              corresponding to one bit of the bitmap. Granularity must be
              a power of 2.

  28 - 31:    l1_size
              The number of entries in the L1 table of the bitmap.

  variable:   L1 offset table (l1_table), size: 8 * l1_size bytes

The dirty bitmap described by this feature extension is stored in a set of
clusters inside the Parallels image file. The offsets of these clusters are
saved in the L1 offset table specified by the feature extension. Each L1 table
entry is a 64 bit integer as described below:

Given an offset in bytes into the bitmap data, corresponding L1 entry is::

    l1_table[offset / cluster_size]

If an L1 table entry is 0, all bits in the corresponding cluster of the bitmap
are assumed to be 0.

If an L1 table entry is 1, all bits in the corresponding cluster of the bitmap
are assumed to be 1.

If an L1 table entry is not 0 or 1, it contains the corresponding cluster
offset (in 512b sectors). Given an offset in bytes into the bitmap data the
offset in bytes into the image file can be obtained as follows::

    offset = l1_table[offset / cluster_size] * 512 + (offset % cluster_size)
