Boundary Devices SABRE Lite (``sabrelite``)
===========================================

Boundary Devices SABRE Lite i.MX6 Development Board is a low-cost development
platform featuring the powerful Freescale / NXP Semiconductor's i.MX 6 Quad
Applications Processor.

Supported devices
-----------------

The SABRE Lite machine supports the following devices:

 * Up to 4 Cortex A9 cores
 * Generic Interrupt Controller
 * 1 Clock Controller Module
 * 1 System Reset Controller
 * 5 UARTs
 * 2 EPIC timers
 * 1 GPT timer
 * 2 Watchdog timers
 * 1 FEC Ethernet controller
 * 3 I2C controllers
 * 7 GPIO controllers
 * 4 SDHC storage controllers
 * 4 USB 2.0 host controllers
 * 5 ECSPI controllers
 * 1 SST 25VF016B flash

Please note above list is a complete superset the QEMU SABRE Lite machine can
support. For a normal use case, a device tree blob that represents a real world
SABRE Lite board, only exposes a subset of devices to the guest software.

Boot options
------------

The SABRE Lite machine can start using the standard -kernel functionality
for loading a Linux kernel, U-Boot bootloader or ELF executable.

Running Linux kernel
--------------------

Linux mainline v5.10 release is tested at the time of writing. To build a Linux
mainline kernel that can be booted by the SABRE Lite machine, simply configure
the kernel using the imx_v6_v7_defconfig configuration:

.. code-block:: bash

  $ export ARCH=arm
  $ export CROSS_COMPILE=arm-linux-gnueabihf-
  $ make imx_v6_v7_defconfig
  $ make

To boot the newly built Linux kernel in QEMU with the SABRE Lite machine, use:

.. code-block:: bash

  $ qemu-system-arm -M sabrelite -smp 4 -m 1G \
      -display none -serial null -serial stdio \
      -kernel arch/arm/boot/zImage \
      -dtb arch/arm/boot/dts/imx6q-sabrelite.dtb \
      -initrd /path/to/rootfs.ext4 \
      -append "root=/dev/ram"

Running U-Boot
--------------

U-Boot mainline v2020.10 release is tested at the time of writing. To build a
U-Boot mainline bootloader that can be booted by the SABRE Lite machine, use
the mx6qsabrelite_defconfig with similar commands as described above for Linux:

.. code-block:: bash

  $ export CROSS_COMPILE=arm-linux-gnueabihf-
  $ make mx6qsabrelite_defconfig

Note we need to adjust settings by:

.. code-block:: bash

  $ make menuconfig

then manually select the following configuration in U-Boot:

  Device Tree Control > Provider of DTB for DT Control > Embedded DTB

To start U-Boot using the SABRE Lite machine, provide the u-boot binary to
the -kernel argument, along with an SD card image with rootfs:

.. code-block:: bash

  $ qemu-system-arm -M sabrelite -smp 4 -m 1G \
      -display none -serial null -serial stdio \
      -kernel u-boot

The following example shows booting Linux kernel from dhcp, and uses the
rootfs on an SD card. This requires some additional command line parameters
for QEMU:

.. code-block:: none

  -nic user,tftp=/path/to/kernel/zImage \
  -drive file=sdcard.img,id=rootfs -device sd-card,drive=rootfs

The directory for the built-in TFTP server should also contain the device tree
blob of the SABRE Lite board. The sample SD card image was populated with the
root file system with one single partition. You may adjust the kernel "root="
boot parameter accordingly.

After U-Boot boots, type the following commands in the U-Boot command shell to
boot the Linux kernel:

.. code-block:: none

  => setenv ethaddr 00:11:22:33:44:55
  => setenv bootfile zImage
  => dhcp
  => tftpboot 14000000 imx6q-sabrelite.dtb
  => setenv bootargs root=/dev/mmcblk3p1
  => bootz 12000000 - 14000000
