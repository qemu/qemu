Microchip PolarFire SoC Icicle Kit (``microchip-icicle-kit``)
=============================================================

Microchip PolarFire SoC Icicle Kit integrates a PolarFire SoC, with one
SiFive's E51 plus four U54 cores and many on-chip peripherals and an FPGA.

For more details about Microchip PolarFire SoC, please see:
https://www.microsemi.com/product-directory/soc-fpgas/5498-polarfire-soc-fpga

The Icicle Kit board information can be found here:
https://www.microsemi.com/existing-parts/parts/152514

Supported devices
-----------------

The ``microchip-icicle-kit`` machine supports the following devices:

 * 1 E51 core
 * 4 U54 cores
 * Core Level Interruptor (CLINT)
 * Platform-Level Interrupt Controller (PLIC)
 * L2 Loosely Integrated Memory (L2-LIM)
 * DDR memory controller
 * 5 MMUARTs
 * 1 DMA controller
 * 2 GEM Ethernet controllers
 * 1 SDHC storage controller

Boot options
------------

The ``microchip-icicle-kit`` machine can start using the standard -bios
functionality for loading its BIOS image, aka Hart Software Services (HSS_).
HSS loads the second stage bootloader U-Boot from an SD card. It does not
support direct kernel loading via the -kernel option. One has to load kernel
from U-Boot.

The memory is set to 1537 MiB by default which is the minimum required high
memory size by HSS. A sanity check on ram size is performed in the machine
init routine to prompt user to increase the RAM size to > 1537 MiB when less
than 1537 MiB ram is detected.

Boot the machine
----------------

HSS 2020.12 release is tested at the time of writing. To build an HSS image
that can be booted by the ``microchip-icicle-kit`` machine, type the following
in the HSS source tree:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ cp boards/mpfs-icicle-kit-es/def_config .config
  $ make BOARD=mpfs-icicle-kit-es

Download the official SD card image released by Microchip and prepare it for
QEMU usage:

.. code-block:: bash

  $ wget ftp://ftpsoc.microsemi.com/outgoing/core-image-minimal-dev-icicle-kit-es-sd-20201009141623.rootfs.wic.gz
  $ gunzip core-image-minimal-dev-icicle-kit-es-sd-20201009141623.rootfs.wic.gz
  $ qemu-img resize core-image-minimal-dev-icicle-kit-es-sd-20201009141623.rootfs.wic 4G

Then we can boot the machine by:

.. code-block:: bash

  $ qemu-system-riscv64 -M microchip-icicle-kit -smp 5 \
      -bios path/to/hss.bin -sd path/to/sdcard.img \
      -nic user,model=cadence_gem \
      -nic tap,ifname=tap,model=cadence_gem,script=no \
      -display none -serial stdio \
      -chardev socket,id=serial1,path=serial1.sock,server=on,wait=on \
      -serial chardev:serial1

With above command line, current terminal session will be used for the first
serial port. Open another terminal window, and use `minicom` to connect the
second serial port.

.. code-block:: bash

  $ minicom -D unix\#serial1.sock

HSS output is on the first serial port (stdio) and U-Boot outputs on the
second serial port. U-Boot will automatically load the Linux kernel from
the SD card image.

.. _HSS: https://github.com/polarfire-soc/hart-software-services
