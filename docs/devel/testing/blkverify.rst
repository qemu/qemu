Block driver correctness testing with ``blkverify``
===================================================

Introduction
------------

This document describes how to use the ``blkverify`` protocol to test that a block
driver is operating correctly.

It is difficult to test and debug block drivers against real guests.  Often
processes inside the guest will crash because corrupt sectors were read as part
of the executable.  Other times obscure errors are raised by a program inside
the guest.  These issues are extremely hard to trace back to bugs in the block
driver.

``blkverify`` solves this problem by catching data corruption inside QEMU the first
time bad data is read and reporting the disk sector that is corrupted.

How it works
------------

The ``blkverify`` protocol has two child block devices, the "test" device and the
"raw" device.  Read/write operations are mirrored to both devices so their
state should always be in sync.

The "raw" device is a raw image, a flat file, that has identical starting
contents to the "test" image.  The idea is that the "raw" device will handle
read/write operations correctly and not corrupt data.  It can be used as a
reference for comparison against the "test" device.

After a mirrored read operation completes, ``blkverify`` will compare the data and
raise an error if it is not identical.  This makes it possible to catch the
first instance where corrupt data is read.

Example
-------

Imagine raw.img has 0xcd repeated throughout its first sector::

    $ ./qemu-io -c 'read -v 0 512' raw.img
    00000000:  cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ................
    00000010:  cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ................
    [...]
    000001e0:  cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ................
    000001f0:  cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd cd  ................
    read 512/512 bytes at offset 0
    512.000000 bytes, 1 ops; 0.0000 sec (97.656 MiB/sec and 200000.0000 ops/sec)

And test.img is corrupt, its first sector is zeroed when it shouldn't be::

    $ ./qemu-io -c 'read -v 0 512' test.img
    00000000:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
    00000010:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
    [...]
    000001e0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
    000001f0:  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
    read 512/512 bytes at offset 0
    512.000000 bytes, 1 ops; 0.0000 sec (81.380 MiB/sec and 166666.6667 ops/sec)

This error is caught by ``blkverify``::

    $ ./qemu-io -c 'read 0 512' blkverify:a.img:b.img
    blkverify: read sector_num=0 nb_sectors=4 contents mismatch in sector 0

A more realistic scenario is verifying the installation of a guest OS::

    $ ./qemu-img create raw.img 16G
    $ ./qemu-img create -f qcow2 test.qcow2 16G
    $ ./qemu-system-x86_64 -cdrom debian.iso \
          -drive file=blkverify:raw.img:test.qcow2

If the installation is aborted when ``blkverify`` detects corruption, use ``qemu-io``
to explore the contents of the disk image at the sector in question.
