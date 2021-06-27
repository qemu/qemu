SiFive HiFive Unleashed (``sifive_u``)
======================================

SiFive HiFive Unleashed Development Board is the ultimate RISC-V development
board featuring the Freedom U540 multi-core RISC-V processor.

Supported devices
-----------------

The ``sifive_u`` machine supports the following devices:

* 1 E51 / E31 core
* Up to 4 U54 / U34 cores
* Core Local Interruptor (CLINT)
* Platform-Level Interrupt Controller (PLIC)
* Power, Reset, Clock, Interrupt (PRCI)
* L2 Loosely Integrated Memory (L2-LIM)
* DDR memory controller
* 2 UARTs
* 1 GEM Ethernet controller
* 1 GPIO controller
* 1 One-Time Programmable (OTP) memory with stored serial number
* 1 DMA controller
* 2 QSPI controllers
* 1 ISSI 25WP256 flash
* 1 SD card in SPI mode

Please note the real world HiFive Unleashed board has a fixed configuration of
1 E51 core and 4 U54 core combination and the RISC-V core boots in 64-bit mode.
With QEMU, one can create a machine with 1 E51 core and up to 4 U54 cores. It
is also possible to create a 32-bit variant with the same peripherals except
that the RISC-V cores are replaced by the 32-bit ones (E31 and U34), to help
testing of 32-bit guest software.

Hardware configuration information
----------------------------------

The ``sifive_u`` machine automatically generates a device tree blob ("dtb")
which it passes to the guest, if there is no ``-dtb`` option. This provides
information about the addresses, interrupt lines and other configuration of
the various devices in the system. Guest software should discover the devices
that are present in the generated DTB instead of using a DTB for the real
hardware, as some of the devices are not modeled by QEMU and trying to access
these devices may cause unexpected behavior.

If users want to provide their own DTB, they can use the ``-dtb`` option.
These DTBs should have the following requirements:

* The /cpus node should contain at least one subnode for E51 and the number
  of subnodes should match QEMU's ``-smp`` option
* The /memory reg size should match QEMU’s selected ram_size via ``-m``
* Should contain a node for the CLINT device with a compatible string
  "riscv,clint0" if using with OpenSBI BIOS images

Boot options
------------

The ``sifive_u`` machine can start using the standard -kernel functionality
for loading a Linux kernel, a VxWorks kernel, a modified U-Boot bootloader
(S-mode) or ELF executable with the default OpenSBI firmware image as the
-bios. It also supports booting the unmodified U-Boot bootloader using the
standard -bios functionality.

Machine-specific options
------------------------

The following machine-specific options are supported:

- serial=nnn

  The board serial number. When not given, the default serial number 1 is used.

  SiFive reserves the first 1 KiB of the 16 KiB OTP memory for internal use.
  The current usage is only used to store the serial number of the board at
  offset 0xfc. U-Boot reads the serial number from the OTP memory, and uses
  it to generate a unique MAC address to be programmed to the on-chip GEM
  Ethernet controller. When multiple QEMU ``sifive_u`` machines are created
  and connected to the same subnet, they all have the same MAC address hence
  it creates an unusable network. In such scenario, user should give different
  values to serial= when creating different ``sifive_u`` machines.

- start-in-flash

  When given, QEMU's ROM codes jump to QSPI memory-mapped flash directly.
  Otherwise QEMU will jump to DRAM or L2LIM depending on the msel= value.
  When not given, it defaults to direct DRAM booting.

- msel=[6|11]

  Mode Select (MSEL[3:0]) pins value, used to control where to boot from.

  The FU540 SoC supports booting from several sources, which are controlled
  using the Mode Select pins on the chip. Typically, the boot process runs
  through several stages before it begins execution of user-provided programs.
  These stages typically include the following:

  1. Zeroth Stage Boot Loader (ZSBL), which is contained in an on-chip mask
     ROM and provided by QEMU. Note QEMU implemented ROM codes are not the
     same as what is programmed in the hardware. The QEMU one is a simplified
     version, but it provides the same functionality as the hardware.
  2. First Stage Boot Loader (FSBL), which brings up PLLs and DDR memory.
     This is U-Boot SPL.
  3. Second Stage Boot Loader (SSBL), which further initializes additional
     peripherals as needed. This is U-Boot proper combined with an OpenSBI
     fw_dynamic firmware image.

  msel=6 means FSBL and SSBL are both on the QSPI flash. msel=11 means FSBL
  and SSBL are both on the SD card.

Running Linux kernel
--------------------

Linux mainline v5.10 release is tested at the time of writing. To build a
Linux mainline kernel that can be booted by the ``sifive_u`` machine in
64-bit mode, simply configure the kernel using the defconfig configuration:

.. code-block:: bash

  $ export ARCH=riscv
  $ export CROSS_COMPILE=riscv64-linux-
  $ make defconfig
  $ make

To boot the newly built Linux kernel in QEMU with the ``sifive_u`` machine:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u -smp 5 -m 2G \
      -display none -serial stdio \
      -kernel arch/riscv/boot/Image \
      -initrd /path/to/rootfs.ext4 \
      -append "root=/dev/ram"

Alternatively, we can use a custom DTB to boot the machine by inserting a CLINT
node in fu540-c000.dtsi in the Linux kernel,

.. code-block:: none

    clint: clint@2000000 {
        compatible = "riscv,clint0";
        interrupts-extended = <&cpu0_intc 3 &cpu0_intc 7
                               &cpu1_intc 3 &cpu1_intc 7
                               &cpu2_intc 3 &cpu2_intc 7
                               &cpu3_intc 3 &cpu3_intc 7
                               &cpu4_intc 3 &cpu4_intc 7>;
        reg = <0x00 0x2000000 0x00 0x10000>;
    };

with the following command line options:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u -smp 5 -m 8G \
      -display none -serial stdio \
      -kernel arch/riscv/boot/Image \
      -dtb arch/riscv/boot/dts/sifive/hifive-unleashed-a00.dtb \
      -initrd /path/to/rootfs.ext4 \
      -append "root=/dev/ram"

To build a Linux mainline kernel that can be booted by the ``sifive_u`` machine
in 32-bit mode, use the rv32_defconfig configuration. A patch is required to
fix the 32-bit boot issue for Linux kernel v5.10.

.. code-block:: bash

  $ export ARCH=riscv
  $ export CROSS_COMPILE=riscv64-linux-
  $ curl https://patchwork.kernel.org/project/linux-riscv/patch/20201219001356.2887782-1-atish.patra@wdc.com/mbox/ > riscv.patch
  $ git am riscv.patch
  $ make rv32_defconfig
  $ make

Replace ``qemu-system-riscv64`` with ``qemu-system-riscv32`` in the command
line above to boot the 32-bit Linux kernel. A rootfs image containing 32-bit
applications shall be used in order for kernel to boot to user space.

Running VxWorks kernel
----------------------

VxWorks 7 SR0650 release is tested at the time of writing. To build a 64-bit
VxWorks mainline kernel that can be booted by the ``sifive_u`` machine, simply
create a VxWorks source build project based on the sifive_generic BSP, and a
VxWorks image project to generate the bootable VxWorks image, by following the
BSP documentation instructions.

A pre-built 64-bit VxWorks 7 image for HiFive Unleashed board is available as
part of the VxWorks SDK for testing as well. Instructions to download the SDK:

.. code-block:: bash

  $ wget https://labs.windriver.com/downloads/wrsdk-vxworks7-sifive-hifive-1.01.tar.bz2
  $ tar xvf wrsdk-vxworks7-sifive-hifive-1.01.tar.bz2
  $ ls bsps/sifive_generic_1_0_0_0/uboot/uVxWorks

To boot the VxWorks kernel in QEMU with the ``sifive_u`` machine, use:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u -smp 5 -m 2G \
      -display none -serial stdio \
      -nic tap,ifname=tap0,script=no,downscript=no \
      -kernel /path/to/vxWorks \
      -append "gem(0,0)host:vxWorks h=192.168.200.1 e=192.168.200.2:ffffff00 u=target pw=vxTarget f=0x01"

It is also possible to test 32-bit VxWorks on the ``sifive_u`` machine. Create
a 32-bit project to build the 32-bit VxWorks image, and use exact the same
command line options with ``qemu-system-riscv32``.

Running U-Boot
--------------

U-Boot mainline v2021.01 release is tested at the time of writing. To build a
U-Boot mainline bootloader that can be booted by the ``sifive_u`` machine, use
the sifive_fu540_defconfig with similar commands as described above for Linux:

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ export OPENSBI=/path/to/opensbi-riscv64-generic-fw_dynamic.bin
  $ make sifive_fu540_defconfig

You will get spl/u-boot-spl.bin and u-boot.itb file in the build tree.

To start U-Boot using the ``sifive_u`` machine, prepare an SPI flash image, or
SD card image that is properly partitioned and populated with correct contents.
genimage_ can be used to generate these images.

A sample configuration file for a 128 MiB SD card image is:

.. code-block:: bash

  $ cat genimage_sdcard.cfg
  image sdcard.img {
          size = 128M

          hdimage {
                  gpt = true
          }

          partition u-boot-spl {
                  image = "u-boot-spl.bin"
                  offset = 17K
                  partition-type-uuid = 5B193300-FC78-40CD-8002-E86C45580B47
          }

          partition u-boot {
                  image = "u-boot.itb"
                  offset = 1041K
                  partition-type-uuid = 2E54B353-1271-4842-806F-E436D6AF6985
          }
  }

SPI flash image has slightly different partition offsets, and the size has to
be 32 MiB to match the ISSI 25WP256 flash on the real board:

.. code-block:: bash

  $ cat genimage_spi-nor.cfg
  image spi-nor.img {
          size = 32M

          hdimage {
                  gpt = true
          }

          partition u-boot-spl {
                  image = "u-boot-spl.bin"
                  offset = 20K
                  partition-type-uuid = 5B193300-FC78-40CD-8002-E86C45580B47
          }

          partition u-boot {
                  image = "u-boot.itb"
                  offset = 1044K
                  partition-type-uuid = 2E54B353-1271-4842-806F-E436D6AF6985
          }
  }

Assume U-Boot binaries are put in the same directory as the config file,
we can generate the image by:

.. code-block:: bash

  $ genimage --config genimage_<boot_src>.cfg --inputpath .

Boot U-Boot from SD card, by specifying msel=11 and pass the SD card image
to QEMU ``sifive_u`` machine:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u,msel=11 -smp 5 -m 8G \
      -display none -serial stdio \
      -bios /path/to/u-boot-spl.bin \
      -drive file=/path/to/sdcard.img,if=sd

Changing msel= value to 6, allows booting U-Boot from the SPI flash:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u,msel=6 -smp 5 -m 8G \
      -display none -serial stdio \
      -bios /path/to/u-boot-spl.bin \
      -drive file=/path/to/spi-nor.img,if=mtd

Note when testing U-Boot, QEMU automatically generated device tree blob is
not used because U-Boot itself embeds device tree blobs for U-Boot SPL and
U-Boot proper. Hence the number of cores and size of memory have to match
the real hardware, ie: 5 cores (-smp 5) and 8 GiB memory (-m 8G).

Above use case is to run upstream U-Boot for the SiFive HiFive Unleashed
board on QEMU ``sifive_u`` machine out of the box. This allows users to
develop and test the recommended RISC-V boot flow with a real world use
case: ZSBL (in QEMU) loads U-Boot SPL from SD card or SPI flash to L2LIM,
then U-Boot SPL loads the combined payload image of OpenSBI fw_dynamic
firmware and U-Boot proper. However sometimes we want to have a quick test
of booting U-Boot on QEMU without the needs of preparing the SPI flash or
SD card images, an alternate way can be used, which is to create a U-Boot
S-mode image by modifying the configuration of U-Boot:

.. code-block:: bash

  $ make menuconfig

then manually select the following configuration in U-Boot:

  Device Tree Control > Provider of DTB for DT Control > Prior Stage bootloader DTB

This lets U-Boot to use the QEMU generated device tree blob. During the build,
a build error will be seen below:

.. code-block:: none

  MKIMAGE u-boot.img
  ./tools/mkimage: Can't open arch/riscv/dts/hifive-unleashed-a00.dtb: No such file or directory
  ./tools/mkimage: failed to build FIT
  make: *** [Makefile:1440: u-boot.img] Error 1

The above errors can be safely ignored as we don't run U-Boot SPL under QEMU
in this alternate configuration.

Boot the 64-bit U-Boot S-mode image directly:

.. code-block:: bash

  $ qemu-system-riscv64 -M sifive_u -smp 5 -m 2G \
      -display none -serial stdio \
      -kernel /path/to/u-boot.bin

It's possible to create a 32-bit U-Boot S-mode image as well.

.. code-block:: bash

  $ export CROSS_COMPILE=riscv64-linux-
  $ make sifive_fu540_defconfig
  $ make menuconfig

then manually update the following configuration in U-Boot:

  Device Tree Control > Provider of DTB for DT Control > Prior Stage bootloader DTB
  RISC-V architecture > Base ISA > RV32I
  Boot images > Text Base > 0x80400000

Use the same command line options to boot the 32-bit U-Boot S-mode image:

.. code-block:: bash

  $ qemu-system-riscv32 -M sifive_u -smp 5 -m 2G \
      -display none -serial stdio \
      -kernel /path/to/u-boot.bin

.. _genimage: https://github.com/pengutronix/genimage
