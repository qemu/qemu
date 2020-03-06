.. _disk_005fimages:

Disk Images
-----------

QEMU supports many disk image formats, including growable disk images
(their size increase as non empty sectors are written), compressed and
encrypted disk images.

.. _disk_005fimages_005fquickstart:

Quick start for disk image creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can create a disk image with the command::

   qemu-img create myimage.img mysize

where myimage.img is the disk image filename and mysize is its size in
kilobytes. You can add an ``M`` suffix to give the size in megabytes and
a ``G`` suffix for gigabytes.

See the qemu-img invocation documentation for more information.

.. _disk_005fimages_005fsnapshot_005fmode:

Snapshot mode
~~~~~~~~~~~~~

If you use the option ``-snapshot``, all disk images are considered as
read only. When sectors in written, they are written in a temporary file
created in ``/tmp``. You can however force the write back to the raw
disk images by using the ``commit`` monitor command (or C-a s in the
serial console).

.. _vm_005fsnapshots:

VM snapshots
~~~~~~~~~~~~

VM snapshots are snapshots of the complete virtual machine including CPU
state, RAM, device state and the content of all the writable disks. In
order to use VM snapshots, you must have at least one non removable and
writable block device using the ``qcow2`` disk image format. Normally
this device is the first virtual hard drive.

Use the monitor command ``savevm`` to create a new VM snapshot or
replace an existing one. A human readable name can be assigned to each
snapshot in addition to its numerical ID.

Use ``loadvm`` to restore a VM snapshot and ``delvm`` to remove a VM
snapshot. ``info snapshots`` lists the available snapshots with their
associated information::

   (qemu) info snapshots
   Snapshot devices: hda
   Snapshot list (from hda):
   ID        TAG                 VM SIZE                DATE       VM CLOCK
   1         start                   41M 2006-08-06 12:38:02   00:00:14.954
   2                                 40M 2006-08-06 12:43:29   00:00:18.633
   3         msys                    40M 2006-08-06 12:44:04   00:00:23.514

A VM snapshot is made of a VM state info (its size is shown in
``info snapshots``) and a snapshot of every writable disk image. The VM
state info is stored in the first ``qcow2`` non removable and writable
block device. The disk image snapshots are stored in every disk image.
The size of a snapshot in a disk image is difficult to evaluate and is
not shown by ``info snapshots`` because the associated disk sectors are
shared among all the snapshots to save disk space (otherwise each
snapshot would need a full copy of all the disk images).

When using the (unrelated) ``-snapshot`` option
(:ref:`disk_005fimages_005fsnapshot_005fmode`),
you can always make VM snapshots, but they are deleted as soon as you
exit QEMU.

VM snapshots currently have the following known limitations:

-  They cannot cope with removable devices if they are removed or
   inserted after a snapshot is done.

-  A few device drivers still have incomplete snapshot support so their
   state is not saved or restored properly (in particular USB).

.. include:: qemu-block-drivers.rst.inc
