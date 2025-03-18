Banana Pi BPI-M2U (``bpim2u``)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Banana Pi BPI-M2 Ultra is a quad-core mini single board computer built with
Allwinner A40i/R40/V40 SoC. It features 2GB of RAM and 8GB eMMC. It also
has onboard WiFi and BT. On the ports side, the BPI-M2 Ultra has 2 USB A
2.0 ports, 1 USB OTG port, 1 HDMI port, 1 audio jack, a DC power port,
and last but not least, a SATA port.

Supported devices
"""""""""""""""""

The Banana Pi M2U machine supports the following devices:

 * SMP (Quad Core Cortex-A7)
 * Generic Interrupt Controller configuration
 * SRAM mappings
 * SDRAM controller
 * Timer device (re-used from Allwinner A10)
 * UART
 * SD/MMC storage controller
 * EMAC ethernet
 * GMAC ethernet
 * Clock Control Unit
 * SATA
 * TWI (I2C)
 * USB 2.0
 * Hardware Watchdog

Limitations
"""""""""""

Currently, Banana Pi M2U does *not* support the following features:

- Graphical output via HDMI, GPU and/or the Display Engine
- Audio output
- Real Time Clock

Also see the 'unimplemented' array in the Allwinner R40 SoC module
for a complete list of unimplemented I/O devices: ``./hw/arm/allwinner-r40.c``

Boot options
""""""""""""

The Banana Pi M2U machine can start using the standard -kernel functionality
for loading a Linux kernel or ELF executable. Additionally, the Banana Pi M2U
machine can also emulate the BootROM which is present on an actual Allwinner R40
based SoC, which loads the bootloader from a SD card, specified via the -sd
argument to qemu-system-arm.

Running mainline Linux
""""""""""""""""""""""

To build a Linux mainline kernel that can be booted by the Banana Pi M2U machine,
simply configure the kernel using the sunxi_defconfig configuration:

.. code-block:: bash

  $ ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make mrproper
  $ ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make sunxi_defconfig

To boot the newly build linux kernel in QEMU with the Banana Pi M2U machine, use:

.. code-block:: bash

  $ qemu-system-arm -M bpim2u -nographic \
      -kernel /path/to/linux/arch/arm/boot/zImage \
      -append 'console=ttyS0,115200' \
      -dtb /path/to/linux/arch/arm/boot/dts/sun8i-r40-bananapi-m2-ultra.dtb

Banana Pi M2U images
""""""""""""""""""""

Note that the mainline kernel does not have a root filesystem. You can choose
to build you own image with buildroot using the bananapi_m2_ultra_defconfig.
Also see https://buildroot.org for more information.

Another possibility is to run an OpenWrt image for Banana Pi M2U which
can be downloaded from:

   https://downloads.openwrt.org/releases/22.03.3/targets/sunxi/cortexa7/

When using an image as an SD card, it must be resized to a power of two. This can be
done with the ``qemu-img`` command. It is recommended to only increase the image size
instead of shrinking it to a power of two, to avoid loss of data. For example,
to prepare a downloaded Armbian image, first extract it and then increase
its size to one gigabyte as follows:

.. code-block:: bash

  $ qemu-img resize \
    openwrt-22.03.3-sunxi-cortexa7-sinovoip_bananapi-m2-ultra-ext4-sdcard.img \
    1G

Instead of providing a custom Linux kernel via the -kernel command you may also
choose to let the Banana Pi M2U machine load the bootloader from SD card, just like
a real board would do using the BootROM. Simply pass the selected image via the -sd
argument and remove the -kernel, -append, -dbt and -initrd arguments:

.. code-block:: bash

  $ qemu-system-arm -M bpim2u -nic user -nographic \
    -sd openwrt-22.03.3-sunxi-cortexa7-sinovoip_bananapi-m2-ultra-ext4-sdcard.img

Running U-Boot
""""""""""""""

U-Boot mainline can be build and configured using the Bananapi_M2_Ultra_defconfig
using similar commands as describe above for Linux. Note that it is recommended
for development/testing to select the following configuration setting in U-Boot:

  Device Tree Control > Provider for DTB for DT Control > Embedded DTB

The BootROM of allwinner R40 loading u-boot from the 8KiB offset of sdcard.
Let's create an bootable disk image:

.. code-block:: bash

  $ dd if=/dev/zero of=sd.img bs=32M count=1
  $ dd if=u-boot-sunxi-with-spl.bin of=sd.img bs=1k seek=8 conv=notrunc

And then boot it.

.. code-block:: bash

  $ qemu-system-arm -M bpim2u -nographic -sd sd.img

Banana Pi M2U functional tests
""""""""""""""""""""""""""""""

The Banana Pi M2U machine has several functional tests included.
To run the whole set of tests, build QEMU from source and simply
provide the following command:

.. code-block:: bash

  $ cd qemu-build-dir
  $ QEMU_TEST_ALLOW_LARGE_STORAGE=1 \
    pyvenv/bin/meson test --suite thorough func-arm-arm_bpim2u
