ppce500 generic platform (``ppce500``)
======================================

QEMU for PPC supports a special ``ppce500`` machine designed for emulation and
virtualization purposes.

Supported devices
-----------------

The ``ppce500`` machine supports the following devices:

* PowerPC e500 series core (e500v2/e500mc/e5500/e6500)
* Configuration, Control, and Status Register (CCSR)
* Multicore Programmable Interrupt Controller (MPIC) with MSI support
* 1 16550A UART device
* 1 Freescale MPC8xxx I2C controller
* 1 Pericom pt7c4338 RTC via I2C
* 1 Freescale MPC8xxx GPIO controller
* Power-off functionality via one GPIO pin
* 1 Freescale MPC8xxx PCI host controller
* VirtIO devices via PCI bus
* 1 Freescale Enhanced Triple Speed Ethernet controller (eTSEC)

Hardware configuration information
----------------------------------

The ``ppce500`` machine automatically generates a device tree blob ("dtb")
which it passes to the guest, if there is no ``-dtb`` option. This provides
information about the addresses, interrupt lines and other configuration of
the various devices in the system.

If users want to provide their own DTB, they can use the ``-dtb`` option.
These DTBs should have the following requirements:

* The number of subnodes under /cpus node should match QEMU's ``-smp`` option
* The /memory reg size should match QEMU’s selected ram_size via ``-m``

Both ``qemu-system-ppc`` and ``qemu-system-ppc64`` provide emulation for the
following 32-bit PowerPC CPUs:

* e500v2
* e500mc

Additionally ``qemu-system-ppc64`` provides support for the following 64-bit
PowerPC CPUs:

* e5500
* e6500

The CPU type can be specified via the ``-cpu`` command line. If not specified,
it creates a machine with e500v2 core. The following example shows an e6500
based machine creation:

.. code-block:: bash

  $ qemu-system-ppc64 -nographic -M ppce500 -cpu e6500

Boot options
------------

The ``ppce500`` machine can start using the standard -kernel functionality
for loading a payload like an OS kernel (e.g.: Linux), or U-Boot firmware.

When -bios is omitted, the default pc-bios/u-boot.e500 firmware image is used
as the BIOS. QEMU follows below truth table to select which payload to execute:

===== ========== =======
-bios    -kernel payload
===== ========== =======
    N          N  u-boot
    N          Y  kernel
    Y don't care  u-boot
===== ========== =======

When both -bios and -kernel are present, QEMU loads U-Boot and U-Boot in turns
automatically loads the kernel image specified by the -kernel parameter via
U-Boot's built-in "bootm" command, hence a legacy uImage format is required in
such scenario.

Running Linux kernel
--------------------

Linux mainline v5.11 release is tested at the time of writing. To build a
Linux mainline kernel that can be booted by the ``ppce500`` machine in
64-bit mode, simply configure the kernel using the defconfig configuration:

.. code-block:: bash

  $ export ARCH=powerpc
  $ export CROSS_COMPILE=powerpc-linux-
  $ make corenet64_smp_defconfig
  $ make menuconfig

then manually select the following configuration:

  Platform support > Freescale Book-E Machine Type > QEMU generic e500 platform

To boot the newly built Linux kernel in QEMU with the ``ppce500`` machine:

.. code-block:: bash

  $ qemu-system-ppc64 -M ppce500 -cpu e5500 -smp 4 -m 2G \
      -display none -serial stdio \
      -kernel vmlinux \
      -initrd /path/to/rootfs.cpio \
      -append "root=/dev/ram"

To build a Linux mainline kernel that can be booted by the ``ppce500`` machine
in 32-bit mode, use the same 64-bit configuration steps except the defconfig
file should use corenet32_smp_defconfig.

To boot the 32-bit Linux kernel:

.. code-block:: bash

  $ qemu-system-ppc{64|32} -M ppce500 -cpu e500mc -smp 4 -m 2G \
      -display none -serial stdio \
      -kernel vmlinux \
      -initrd /path/to/rootfs.cpio \
      -append "root=/dev/ram"

Running U-Boot
--------------

U-Boot mainline v2021.07 release is tested at the time of writing. To build a
U-Boot mainline bootloader that can be booted by the ``ppce500`` machine, use
the qemu-ppce500_defconfig with similar commands as described above for Linux:

.. code-block:: bash

  $ export CROSS_COMPILE=powerpc-linux-
  $ make qemu-ppce500_defconfig

You will get u-boot file in the build tree.

When U-Boot boots, you will notice the following if using with ``-cpu e6500``:

.. code-block:: none

  CPU:   Unknown, Version: 0.0, (0x00000000)
  Core:  e6500, Version: 2.0, (0x80400020)

This is because we only specified a core name to QEMU and it does not have a
meaningful SVR value which represents an actual SoC that integrates such core.
You can specify a real world SoC device that QEMU has built-in support but all
these SoCs are e500v2 based MPC85xx series, hence you cannot test anything
built for P4080 (e500mc), P5020 (e5500) and T2080 (e6500).

By default a VirtIO standard PCI networking device is connected as an ethernet
interface at PCI address 0.1.0, but we can switch that to an e1000 NIC by:

.. code-block:: bash

  $ qemu-system-ppc -M ppce500 -smp 4 -m 2G \
                    -display none -serial stdio \
                    -bios u-boot \
                    -nic tap,ifname=tap0,script=no,downscript=no,model=e1000

The QEMU ``ppce500`` machine can also dynamically instantiate an eTSEC device
if “-device eTSEC” is given to QEMU:

.. code-block:: bash

  -netdev tap,ifname=tap0,script=no,downscript=no,id=net0 -device eTSEC,netdev=net0
