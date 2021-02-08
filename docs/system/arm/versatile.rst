Arm Versatile boards (``versatileab``, ``versatilepb``)
=======================================================

The Arm Versatile baseboard is emulated with the following devices:

-  ARM926E, ARM1136 or Cortex-A8 CPU

-  PL190 Vectored Interrupt Controller

-  Four PL011 UARTs

-  SMC 91c111 Ethernet adapter

-  PL110 LCD controller

-  PL050 KMI with PS/2 keyboard and mouse.

-  PCI host bridge. Note the emulated PCI bridge only provides access
   to PCI memory space. It does not provide access to PCI IO space. This
   means some devices (eg. ne2k_pci NIC) are not usable, and others (eg.
   rtl8139 NIC) are only usable when the guest drivers use the memory
   mapped control registers.

-  PCI OHCI USB controller.

-  LSI53C895A PCI SCSI Host Bus Adapter with hard disk and CD-ROM
   devices.

-  PL181 MultiMedia Card Interface with SD card.

Booting a Linux kernel
----------------------

Building a current Linux kernel with ``versatile_defconfig`` should be
enough to get something running. Nowadays an out-of-tree build is
recommended (and also useful if you build a lot of different targets).
In the following example $BLD points to the build directory and $SRC
points to the root of the Linux source tree. You can drop $SRC if you
are running from there.

.. code-block:: bash

  $ make O=$BLD -C $SRC ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- versatile_defconfig
  $ make O=$BLD -C $SRC ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

You may want to enable some additional modules if you want to boot
something from the SCSI interface::

  CONFIG_PCI=y
  CONFIG_PCI_VERSATILE=y
  CONFIG_SCSI=y
  CONFIG_SCSI_SYM53C8XX_2=y

You can then boot with a command line like:

.. code-block:: bash

  $ qemu-system-arm -machine type=versatilepb \
      -serial mon:stdio \
      -drive if=scsi,driver=file,filename=debian-buster-armel-rootfs.ext4 \
      -kernel zImage \
      -dtb versatile-pb.dtb  \
      -append "console=ttyAMA0 ro root=/dev/sda"
