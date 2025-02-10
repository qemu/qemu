Managing device boot order with bootindex properties
====================================================

QEMU can tell QEMU-aware guest firmware (like the x86 PC BIOS)
which order it should look for a bootable OS on which devices.
A simple way to set this order is to use the ``-boot order=`` option,
but you can also do this more flexibly, by setting a ``bootindex``
property on the individual block or net devices you specify
on the QEMU command line.

The ``bootindex`` properties are used to determine the order in which
firmware will consider devices for booting the guest OS. If the
``bootindex`` property is not set for a device, it gets the lowest
boot priority. There is no particular order in which devices with no
``bootindex`` property set will be considered for booting, but they
will still be bootable.

Some guest machine types (for instance the s390x machines) do
not support ``-boot order=``; on those machines you must always
use ``bootindex`` properties.

There is no way to set a ``bootindex`` property if you are using
a short-form option like ``-hda`` or ``-cdrom``, so to use
``bootindex`` properties you will need to expand out those options
into long-form ``-drive`` and ``-device`` option pairs.

Example
-------

Let's assume we have a QEMU machine with two NICs (virtio, e1000) and two
disks (IDE, virtio):

.. parsed-literal::

  |qemu_system| -drive file=disk1.img,if=none,id=disk1 \\
                -device ide-hd,drive=disk1,bootindex=4 \\
                -drive file=disk2.img,if=none,id=disk2 \\
                -device virtio-blk-pci,drive=disk2,bootindex=3 \\
                -netdev type=user,id=net0 \\
                -device virtio-net-pci,netdev=net0,bootindex=2 \\
                -netdev type=user,id=net1 \\
                -device e1000,netdev=net1,bootindex=1

Given the command above, firmware should try to boot from the e1000 NIC
first.  If this fails, it should try the virtio NIC next; if this fails
too, it should try the virtio disk, and then the IDE disk.

Limitations
-----------

Some firmware has limitations on which devices can be considered for
booting.  For instance, the x86 PC BIOS boot specification allows only one
disk to be bootable.  If boot from disk fails for some reason, the x86 BIOS
won't retry booting from other disk.  It can still try to boot from
floppy or net, though. In the case of s390x BIOS, the BIOS will try up to
8 total devices, any number of which may be disks or virtio-net devices.

Sometimes, firmware cannot map the device path QEMU wants firmware to
boot from to a boot method.  It doesn't happen for devices the firmware
can natively boot from, but if firmware relies on an option ROM for
booting, and the same option ROM is used for booting from more then one
device, the firmware may not be able to ask the option ROM to boot from
a particular device reliably.  For instance with the PC BIOS, if a SCSI HBA
has three bootable devices target1, target3, target5 connected to it,
the option ROM will have a boot method for each of them, but it is not
possible to map from boot method back to a specific target.  This is a
shortcoming of the PC BIOS boot specification.

Mixing bootindex and boot order parameters
------------------------------------------

Note that it does not make sense to use the bootindex property together
with the ``-boot order=...`` (or ``-boot once=...``) parameter. The guest
firmware implementations normally either support the one or the other,
but not both parameters at the same time. Mixing them will result in
undefined behavior, and thus the guest firmware will likely not boot
from the expected devices.
