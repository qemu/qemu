NXP i.MX 8M Plus Evaluation Kit (``imx8mp-evk``)
================================================

The ``imx8mp-evk`` machine models the i.MX 8M Plus Evaluation Kit, based on an
i.MX 8M Plus SoC.

Supported devices
-----------------

The ``imx8mp-evk`` machine implements the following devices:

 * Up to 4 Cortex-A53 cores
 * Generic Interrupt Controller (GICv3)
 * 4 UARTs
 * 3 USDHC Storage Controllers
 * 1 Designware PCI Express Controller
 * 1 Ethernet Controller
 * 2 Designware USB 3 Controllers
 * 5 GPIO Controllers
 * 6 I2C Controllers
 * 3 SPI Controllers
 * 3 Watchdogs
 * 6 General Purpose Timers
 * Secure Non-Volatile Storage (SNVS) including an RTC
 * Clock Tree

Boot options
------------

The ``imx8mp-evk`` machine can start a Linux kernel directly using the standard
``-kernel`` functionality.

Direct Linux Kernel Boot
''''''''''''''''''''''''

Probably the easiest way to get started with a whole Linux system on the machine
is to generate an image with Buildroot. Version 2024.11.1 is tested at the time
of writing and involves two steps. First run the following commands in the
toplevel directory of the Buildroot source tree:

.. code-block:: bash

  $ make freescale_imx8mpevk_defconfig
  $ make

Once finished successfully there is an ``output/image`` subfolder. Navigate into
it and resize the SD card image to a power of two:

.. code-block:: bash

  $ qemu-img resize sdcard.img 256M

Now that everything is prepared the machine can be started as follows:

.. code-block:: bash

  $ qemu-system-aarch64 -M imx8mp-evk -smp 4 -m 3G \
      -display none -serial null -serial stdio \
      -kernel Image \
      -dtb imx8mp-evk.dtb \
      -append "root=/dev/mmcblk2p2" \
      -drive file=sdcard.img,if=sd,bus=2,format=raw,id=mmcblk2
