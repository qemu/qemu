Parallels Disk Format
=====================

..
   Copyright (c) 2015-2017, Virtuozzo, Inc.
   Authors:
        2015 Denis Lunev <den@openvz.org>
        2015 Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
        2016-2017 Klim Kireev <klim.kireev@virtuozzo.com>
        2016-2017 Edgar Kaziakhmedov <edgar.kaziakhmedov@virtuozzo.com>

   This work is licensed under the terms of the GNU GPL, version 2 or later.
   See the COPYING file in the top-level directory.

This specification contains minimal information about Parallels Disk Format,
which is enough to properly work with QEMU. Nevertheless, Parallels Cloud Server
and Parallels Desktop are able to add some unspecified nodes to the xml and use
them, but they are for internal work and don't affect functionality. Also it
uses auxiliary xml ``Snapshot.xml``, which allows storage of optional snapshot
information, but this doesn't influence open/read/write functionality. QEMU and
other software should not use fields not covered in this document or the
``Snapshot.xml`` file, and must leave them as is.

A Parallels disk consists of two parts: the set of snapshots and the disk
descriptor file, which stores information about all files and snapshots.

Definitions
-----------

Snapshot
  a record of the contents captured at a particular time, capable
  of storing current state. A snapshot has a UUID and a parent UUID.

Snapshot image
  an overlay representing the difference between this
  snapshot and some earlier snapshot.

Overlay
  an image storing the different sectors between two captured states.

Root image
  a snapshot image with no parent, the root of the snapshot tree.

Storage
  the backing storage for a subset of the virtual disk. When
  there is more than one storage in a Parallels disk then that
  is referred to as a split image. In this case every storage
  covers a specific address space area of the disk and has its
  particular root image. Split images are not considered here
  and are not supported. Each storage consists of disk
  parameters and a list of images. The list of images always
  contains a root image and may also contain overlays. The
  root image can be an expandable Parallels image file or
  plain. Overlays must be expandable.

Description file
  ``DiskDescriptor.xml`` stores information about disk parameters,
  snapshots, and storages.

Top Snapshot
  The overlay between actual state and some previous snapshot.
  It is not a snapshot in the classical sense because it
  serves as the active image that the guest writes to.

Sector
  a 512-byte data chunk.

Description file
----------------

All information is placed in a single XML element
``Parallels_disk_image``.
The element has only one attribute, ``Version``, which must be ``1.0``.

The schema of ``DiskDescriptor.xml``::

 <Parallels_disk_image Version="1.0">
    <Disk_Parameters>
        ...
    </Disk_Parameters>
    <StorageData>
        ...
    </StorageData>
    <Snapshots>
        ...
    </Snapshots>
 </Parallels_disk_image>

``Disk_Parameters`` element
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``Disk_Parameters`` element describes the physical layout of the
virtual disk and some general settings.

The ``Disk_Parameters`` element MUST contain the following child elements:

* ``Disk_size`` - number of sectors in the disk,
  desired size of the disk.
* ``Cylinders`` - number of the disk cylinders.
* ``Heads``     - number of the disk heads.
* ``Sectors``   - number of the disk sectors per cylinder
  (sector size is 512 bytes)
  Limitation: The product of the ``Heads``, ``Sectors`` and ``Cylinders``
  values MUST be equal to the value of the Disk_size parameter.
* ``Padding``   - must be 0. Parallels Cloud Server and Parallels Desktop may
  use padding set to 1; however this case is not covered
  by this specification. QEMU and other software should not open
  such disks and should not create them.

``StorageData`` element
^^^^^^^^^^^^^^^^^^^^^^^

This element of the file describes the root image and all snapshot images.

The ``StorageData`` element consists of the ``Storage`` child element,
as shown below::

 <StorageData>
    <Storage>
        ...
    </Storage>
 </StorageData>

A ``Storage`` element has the following child elements:

* ``Start``     - start sector of the storage, in case of non split storage
  equals to 0.
* ``End``       - number of sector following the last sector, in case of non
  split storage equals to ``Disk_size``.
* ``Blocksize`` - storage cluster size, number of sectors per one cluster.
  The cluster size for each "Compressed" (see below) image in
  a parallels disk must be equal to this field. Note: the cluster
  size for a Parallels Expandable Image is in the ``tracks`` field of
  its header (see :doc:`parallels`).
* Several ``Image`` child elements.

Each ``Image`` element has the following child elements:

* ``GUID`` - image identifier, UUID in curly brackets.
  For instance, ``{12345678-9abc-def1-2345-6789abcdef12}.``
  The GUID is used by the Snapshots element to reference images
  (see below)
* ``Type`` - image type of the element. It can be:

  * ``Plain`` for raw files.
  * ``Compressed`` for expanding disks.

* ``File`` - path to image file. The path can be relative to
  ``DiskDescriptor.xml`` or absolute.

``Snapshots`` element
^^^^^^^^^^^^^^^^^^^^^

The ``Snapshots`` element describes the snapshot relations with the snapshot tree.

The element contains the set of ``Shot`` child elements, as shown below::

 <Snapshots>
    <TopGUID> ... </TopGUID> /* Optional child element */
    <Shot>
        ...
    </Shot>
    <Shot>
        ...
    </Shot>
    ...
 </Snapshots>

Each ``Shot`` element contains the following child elements:

* ``GUID``       - an image GUID.
* ``ParentGUID`` - GUID of the image of the parent snapshot.

The software may traverse snapshots from child to parent using the
``<ParentGUID>`` field as reference. The ``ParentGUID`` of the root
snapshot is ``{00000000-0000-0000-0000-000000000000}``.
There should be only one root snapshot.

The Top snapshot could be
described via two ways: via the ``TopGUID`` child
element of the ``Snapshots`` element, or via the predefined GUID
``{5fbaabe3-6958-40ff-92a7-860e329aab41}``. If ``TopGUID`` is defined,
the predefined GUID is interpreted as a normal GUID. All snapshot images
(except the Top Snapshot) should be
opened read-only.

There is another predefined GUID,
``BackupID = {704718e1-2314-44c8-9087-d78ed36b0f4e}``, which is used by
original and some third-party software for backup. QEMU and other
software may operate with images with ``GUID = BackupID`` as usual.
However, it is not recommended to use this
GUID for new disks. The Top snapshot cannot have this GUID.
