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
HSS loads the second stage bootloader U-Boot from an SD card. Then a kernel
can be loaded from U-Boot. It also supports direct kernel booting via the
-kernel option along with the device tree blob via -dtb. When direct kernel
boot is used, the OpenSBI fw_dynamic BIOS image is used to boot a payload
like U-Boot or OS kernel directly.

The user provided DTB should have the following requirements:

* The /cpus node should contain at least one subnode for E51 and the number
  of subnodes should match QEMU's ``-smp`` option
* The /memory reg size should match QEMU’s selected ram_size via ``-m``
* Should contain a node for the CLINT device with a compatible string
  "riscv,clint0"

QEMU follows below truth table to select which payload to execute:

===== ========== ========== =======
-bios    -kernel       -dtb payload
===== ========== ========== =======
    N          N don't care     HSS
    Y don't care don't care     HSS
    N          Y          Y  kernel
===== ========== ========== =======

The memory is set to 1537 MiB by default which is the minimum required high
memory size by HSS. A sanity check on ram size is performed in the machine
init routine to prompt user to increase the RAM size to > 1537 MiB when less
than 1537 MiB ram is detected.

Running HSS
-----------

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
serial port. Open another terminal window, and use ``minicom`` to connect the
second serial port.

.. code-block:: bash

  $ minicom -D unix\#serial1.sock

HSS output is on the first serial port (stdio) and U-Boot outputs on the
second serial port. U-Boot will automatically load the Linux kernel from
the SD card image.

Direct Kernel Boot
------------------

Sometimes we just want to test booting a new kernel, and transforming the
kernel image to the format required by the HSS bootflow is tedious. We can
use '-kernel' for direct kernel booting just like other RISC-V machines do.

In this mode, the OpenSBI fw_dynamic BIOS image for 'generic' platform is
used to boot an S-mode payload like U-Boot or OS kernel directly.

For example, the following commands show building a U-Boot image from U-Boot
mainline v2021.07 for the Microchip Icicle Kit board:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ make microchip_mpfs_icicle_defconfig

Then we can boot the machine by:

.. code-block:: bash

  $ qemu-system-riscv64 -M microchip-icicle-kit -smp 5 -m 2G \
      -sd path/to/sdcard.img \
      -nic user,model=cadence_gem \
      -nic tap,ifname=tap,model=cadence_gem,script=no \
      -display none -serial stdio \
      -kernel path/to/u-boot/build/dir/u-boot.bin \
      -dtb path/to/u-boot/build/dir/u-boot.dtb

CAVEATS:

* Check the "stdout-path" property in the /chosen node in the DTB to determine
  which serial port is used for the serial console, e.g.: if the console is set
  to the second serial port, change to use "-serial null -serial stdio".
* The default U-Boot configuration uses CONFIG_OF_SEPARATE hence the ELF image
  ``u-boot`` cannot be passed to "-kernel" as it does not contain the DTB hence
  ``u-boot.bin`` has to be used which does contain one. To use the ELF image,
  we need to change to CONFIG_OF_EMBED or CONFIG_OF_PRIOR_STAGE.

.. _HSS: https://github.com/polarfire-soc/hart-software-services
