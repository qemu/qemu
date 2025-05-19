Microchip PolarFire SoC Icicle Kit (``microchip-icicle-kit``)
=============================================================

Microchip PolarFire SoC Icicle Kit integrates a PolarFire SoC, with one
SiFive's E51 plus four U54 cores and many on-chip peripherals and an FPGA.

For more details about Microchip PolarFire SoC, please see:
https://www.microchip.com/en-us/products/fpgas-and-plds/system-on-chip-fpgas/polarfire-soc-fpgas

The Icicle Kit board information can be found here:
https://www.microchip.com/en-us/development-tool/mpfs-icicle-kit-es

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

The memory is set to 1537 MiB by default.  A sanity check on RAM size is
performed in the machine init routine to prompt user to increase the RAM size
to > 1537 MiB when less than 1537 MiB RAM is detected.

Boot options
------------

The ``microchip-icicle-kit`` machine provides some options to run a firmware
(BIOS) or a kernel image.  QEMU follows below truth table to select the
firmware:

============= =========== ======================================
-bios          -kernel    firmware
============= =========== ======================================
none                    N this is an error
none                    Y the kernel image
NULL, default           N hss.bin
NULL, default           Y opensbi-riscv64-generic-fw_dynamic.bin
other          don't care the BIOS image
============= =========== ======================================

Direct Kernel Boot
------------------

Use the ``-kernel`` option to directly run a kernel image.  When a direct
kernel boot is requested, a device tree blob may be specified via the ``-dtb``
option.  Unlike other QEMU machines, this machine does not generate a device
tree for the kernel.  It shall be provided by the user.  The user provided DTB
should meet the following requirements:

* The ``/cpus`` node should contain at least one subnode for E51 and the number
  of subnodes should match QEMU's ``-smp`` option.

* The ``/memory`` reg size should match QEMU’s selected RAM size via the ``-m``
  option.

* It should contain a node for the CLINT device with a compatible string
  "riscv,clint0".

When ``-bios`` is not specified or set to ``default``, the OpenSBI
``fw_dynamic`` BIOS image for the ``generic`` platform is used to boot an
S-mode payload like U-Boot or OS kernel directly.

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

Running HSS
-----------

The machine ``microchip-icicle-kit`` used to run the Hart Software Services
(HSS_), however, the HSS development progressed and the QEMU machine
implementation lacks behind.  Currently, running the HSS no longer works.
There is missing support in the clock and memory controller devices.  In
particular, reading from the SD card does not work.

.. _HSS: https://github.com/polarfire-soc/hart-software-services
