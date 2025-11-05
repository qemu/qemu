.. SPDX-License-Identifier: GPL-2.0-or-later

==============
eMMC Emulation
==============

Besides SD card emulation, QEMU also offers an eMMC model as found on many
embedded boards. An eMMC, just like an SD card, is connected to the machine
via an SDHCI controller.

Create eMMC Images
==================

A recent eMMC consists of 4 partitions: 2 boot partitions, 1 Replay protected
Memory Block (RPMB), and the user data area. QEMU expects backing images for
the eMMC to contain those partitions concatenated in exactly that order.
However, the boot partitions as well as the RPMB might be absent if their sizes
are configured to zero.

The eMMC specification defines alignment constraints for the partitions. The
two boot partitions must be of the same size. Furthermore, boot and RPMB
partitions must be multiples of 128 KB with a maximum of 32640 KB for each
boot partition and 16384K for the RPMB partition.

The alignment constrain of the user data area depends on its size. Up to 2
GByte, the size must be a power of 2. From 2 GByte onward, the size has to be
multiples of 512 byte.

QEMU is enforcing those alignment rules before instantiating the device.
Therefore, the provided image has to strictly follow them as well. The helper
script ``scripts/mkemmc.sh`` can be used to create compliant images, with or
without pre-filled partitions. E.g., to create an eMMC image from a firmware
image and an OS image with an empty 2 MByte RPMB, use the following command:

.. code-block:: console

    scripts/mkemmc.sh -b firmware.img -r /dev/zero:2M os.img emmc.img

This will take care of rounding up the partition sizes to the next valid value
and will leave the RPMB and the second boot partition empty (zeroed).

Adding eMMC Devices
===================

An eMMC is either automatically created by a machine model (e.g. Aspeed boards)
or can be user-created when using a PCI-attached SDHCI controller. To
instantiate the eMMC image from the example above in a machine without other
SDHCI controllers while assuming that the firmware needs a boot partitions of
1 MB, use the following options:

.. code-block:: console

    -drive file=emmc.img,if=none,format=raw,id=emmc-img
    -device sdhci-pci
    -device emmc,drive=emmc-img,boot-partition-size=1048576,rpmb-partition-size=2097152
