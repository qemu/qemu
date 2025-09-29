Aspeed family boards (``ast2500-evb``, ``ast2600-evb``, ``ast2700-evb``, ``bletchley-bmc``, ``fuji-bmc``, ``gb200nvl-bmc``, ``fby35-bmc``, ``fp5280g2-bmc``, ``g220a-bmc``, ``palmetto-bmc``, ``qcom-dc-scm-v1-bmc``, ``qcom-firework-bmc``, ``quanta-q71l-bmc``, ``rainier-bmc``, ``romulus-bmc``, ``sonorapass-bmc``, ``supermicrox11-bmc``, ``supermicrox11spi-bmc``, ``tiogapass-bmc``, ``witherspoon-bmc``, ``yosemitev2-bmc``)
====================================================================================================================================================================================================================================================================================================================================================================================================================================

The QEMU Aspeed machines model BMCs of various OpenPOWER systems and
Aspeed evaluation boards. They are based on different releases of the
Aspeed SoC : the AST2400 integrating an ARM926EJ-S CPU (400MHz), the
AST2500 with an ARM1176JZS CPU (800MHz), the AST2600
with dual cores ARM Cortex-A7 CPUs (1.2GHz).

The SoC comes with RAM, Gigabit ethernet, USB, SD/MMC, USB, SPI, I2C,
etc.

AST2400 SoC based machines :

- ``palmetto-bmc``         OpenPOWER Palmetto POWER8 BMC
- ``quanta-q71l-bmc``      OpenBMC Quanta BMC
- ``supermicrox11-bmc``    Supermicro X11 BMC (ARM926EJ-S)
- ``supermicrox11spi-bmc``    Supermicro X11 SPI BMC (ARM1176)

AST2500 SoC based machines :

- ``ast2500-evb``          Aspeed AST2500 Evaluation board
- ``romulus-bmc``          OpenPOWER Romulus POWER9 BMC
- ``witherspoon-bmc``      OpenPOWER Witherspoon POWER9 BMC
- ``sonorapass-bmc``       OCP SonoraPass BMC
- ``fp5280g2-bmc``         Inspur FP5280G2 BMC
- ``g220a-bmc``            Bytedance G220A BMC
- ``yosemitev2-bmc``       Facebook YosemiteV2 BMC
- ``tiogapass-bmc``        Facebook Tiogapass BMC

AST2600 SoC based machines :

- ``ast2600-evb``          Aspeed AST2600 Evaluation board (Cortex-A7)
- ``rainier-bmc``          IBM Rainier POWER10 BMC
- ``fuji-bmc``             Facebook Fuji BMC
- ``bletchley-bmc``        Facebook Bletchley BMC
- ``fby35-bmc``            Facebook fby35 BMC
- ``gb200nvl-bmc``         Nvidia GB200nvl BMC
- ``qcom-dc-scm-v1-bmc``   Qualcomm DC-SCM V1 BMC
- ``qcom-firework-bmc``    Qualcomm Firework BMC

Supported devices
-----------------

 * SMP (for the AST2600 Cortex-A7)
 * Interrupt Controller (VIC)
 * Timer Controller
 * RTC Controller
 * I2C Controller, including the new register interface of the AST2600
 * System Control Unit (SCU)
 * SRAM mapping
 * X-DMA Controller (basic interface)
 * Static Memory Controller (SMC or FMC) - Only SPI Flash support
 * SPI Memory Controller
 * USB 2.0 Controller
 * SD/MMC storage controllers
 * SDRAM controller (dummy interface for basic settings and training)
 * Watchdog Controller
 * GPIO Controller (Master only)
 * UART
 * Ethernet controllers
 * Front LEDs (PCA9552 on I2C bus)
 * LPC Peripheral Controller (a subset of subdevices are supported)
 * Hash/Crypto Engine (HACE) - Hash support only. TODO: HMAC and RSA
 * ADC
 * Secure Boot Controller (AST2600)
 * eMMC Boot Controller (dummy)
 * PECI Controller (minimal)
 * I3C Controller
 * Internal Bridge Controller (SLI dummy)


Missing devices
---------------

 * Coprocessor support
 * PWM and Fan Controller
 * Slave GPIO Controller
 * Super I/O Controller
 * PCI-Express 1 Controller
 * Graphic Display Controller
 * MCTP Controller
 * Mailbox Controller
 * Virtual UART
 * eSPI Controller

Boot options
------------

The Aspeed machines can be started using the ``-kernel`` and ``-dtb`` options
to load a Linux kernel or from a firmware. Images can be downloaded from the
OpenBMC jenkins :

   https://jenkins.openbmc.org/job/ci-openbmc/lastSuccessfulBuild/

or directly from the OpenBMC GitHub release repository :

   https://github.com/openbmc/openbmc/releases

or directly from the ASPEED Forked OpenBMC GitHub release repository :

   https://github.com/AspeedTech-BMC/openbmc/releases

Booting from a kernel image
^^^^^^^^^^^^^^^^^^^^^^^^^^^

To boot a kernel directly from a Linux build tree:

.. code-block:: bash

  $ qemu-system-arm -M ast2600-evb -nographic \
        -kernel arch/arm/boot/zImage \
        -dtb arch/arm/boot/dts/aspeed-ast2600-evb.dtb \
        -initrd rootfs.cpio

Booting from a flash image
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The machine options specific to Aspeed to boot from a flash image are :

 * ``execute-in-place`` which emulates the boot from the CE0 flash
   device by using the FMC controller to load the instructions, and
   not simply from RAM. This takes a little longer.

 * ``fmc-model`` to change the default FMC Flash model. FW needs
   support for the chip model to boot.

 * ``spi-model`` to change the default SPI Flash model.

To boot the machine from the flash image, use an MTD drive :

.. code-block:: bash

  $ qemu-system-arm -M romulus-bmc -nic user \
	-drive file=obmc-phosphor-image-romulus.static.mtd,format=raw,if=mtd -nographic

To use other flash models, for instance a different FMC chip and a
bigger (64M) SPI for the ``ast2500-evb`` machine, run :

.. code-block:: bash

  -M ast2500-evb,fmc-model=mx25l25635e,spi-model=mx66u51235f

When more flexibility is needed to define the flash devices, to use
different flash models or define all flash devices (up to 8), the
``-nodefaults`` QEMU option can be used to avoid creating the default
flash devices.

Flash devices should then be created from the command line and attached
to a block device :

.. code-block:: bash

  $ qemu-system-arm -M ast2600-evb \
        -blockdev node-name=fmc0,driver=file,filename=/path/to/fmc0.img \
	-device mx66u51235f,bus=ssi.0,cs=0x0,drive=fmc0 \
	-blockdev node-name=fmc1,driver=file,filename=/path/to/fmc1.img \
	-device mx66u51235f,bus=ssi.0,cs=0x1,drive=fmc1 \
	-blockdev node-name=spi1,driver=file,filename=/path/to/spi1.img \
	-device mx66u51235f,cs=0x0,bus=ssi.1,drive=spi1 \
	-nographic -nodefaults

In that case, the machine boots fetching instructions from the FMC0
device. It is slower to start but closer to what HW does. Using the
machine option ``execute-in-place`` has a similar effect.

Booting from an eMMC image
^^^^^^^^^^^^^^^^^^^^^^^^^^

The machine options specific to Aspeed machines to boot from an eMMC
image are :

 * ``boot-emmc`` to set or unset boot from eMMC (AST2600).

Only the ``ast2600-evb`` and ``rainier-emmc`` machines have support to
boot from an eMMC device. In this case, the machine assumes that the
eMMC image includes special boot partitions. Such an image can be
built this way :

.. code-block:: bash

   $ dd if=/dev/zero of=mmc-bootarea.img count=2 bs=1M
   $ dd if=u-boot-spl.bin of=mmc-bootarea.img conv=notrunc
   $ dd if=u-boot.bin of=mmc-bootarea.img conv=notrunc count=64 bs=1K
   $ cat mmc-bootarea.img obmc-phosphor-image.wic > mmc.img
   $ truncate --size 16GB mmc.img

Boot the machine ``rainier-emmc`` with :

.. code-block:: bash

   $ qemu-system-arm -M rainier-bmc \
         -drive file=mmc.img,format=raw,if=sd,index=2 \
         -nographic

The ``boot-emmc`` option can be set or unset, to change the default
boot mode of machine: SPI or eMMC. This can be useful to boot the
``ast2600-evb`` machine from an eMMC device (default being SPI) or to
boot the ``rainier-bmc`` machine from a flash device (default being
eMMC).

As an example, here is how to to boot the ``rainier-bmc`` machine from
the flash device with ``boot-emmc=false`` and let the machine use an
eMMC image :

.. code-block:: bash

   $ qemu-system-arm -M rainier-bmc,boot-emmc=false \
        -drive file=flash.img,format=raw,if=mtd \
        -drive file=mmc.img,format=raw,if=sd,index=2 \
        -nographic

It should be noted that in this case the eMMC device must not have
boot partitions, otherwise the contents will not be accessible to the
machine.  This limitation is due to the use of the ``-drive``
interface.

Ideally, one should be able to define the eMMC device and the
associated backend directly on the command line, such as :

.. code-block:: bash

   -blockdev node-name=emmc0,driver=file,filename=mmc.img \
   -device emmc,bus=sdhci-bus.2,drive=emmc0,boot-partition-size=1048576,boot-config=8

This is not yet supported (as of QEMU-10.0). Work is needed to
refactor the sdhci bus model.

Other booting options
^^^^^^^^^^^^^^^^^^^^^

Other machine options specific to Aspeed machines are :

 * ``bmc-console`` to change the default console device. Most of the
   machines use the ``UART5`` device for a boot console, which is
   mapped on ``/dev/ttyS4`` under Linux, but it is not always the
   case.

To change the boot console and use device ``UART3`` (``/dev/ttyS2``
under Linux), use :

.. code-block:: bash

  -M ast2500-evb,bmc-console=uart3

OTP Option
^^^^^^^^^^

Both the AST2600 and AST1030 chips use the same One Time Programmable
(OTP) memory module, which is utilized for configuration, key storage,
and storing user-programmable data. This OTP memory module is managed
by the Secure Boot Controller (SBC). The following options can be
specified or omitted based on your needs.

  * When the options are specified, the pre-generated configuration
    file will be used as the OTP memory storage.

  * When the options are omitted, an internal memory buffer will be
    used to store the OTP memory data.

.. code-block:: bash

  -blockdev driver=file,filename=otpmem.img,node-name=otp \
  -global aspeed-otp.drive=otp \

The following bash command can be used to generate a default
configuration file for OTP memory:

.. code-block:: bash

  if [ ! -f otpmem.img ]; then
    for i in $(seq 1 2048); do
      printf '\x00\x00\x00\x00\xff\xff\xff\xff'
    done > otpmem.img
  fi

Aspeed 2700 family boards (``ast2700-evb``)
==================================================================

The QEMU Aspeed machines model BMCs of Aspeed evaluation boards.
They are based on different releases of the Aspeed SoC :
the AST2700 with quad cores ARM Cortex-A35 64 bits CPUs (1.6GHz).

The SoC comes with RAM, Gigabit ethernet, USB, SD/MMC, USB, SPI, I2C,
etc.

AST2700 SoC based machines :

- ``ast2700-evb``          Aspeed AST2700 Evaluation board (Cortex-A35)
- ``ast2700fc``            Aspeed AST2700 Evaluation board (Cortex-A35 + Cortex-M4)

Supported devices
-----------------
 * Interrupt Controller
 * Timer Controller
 * RTC Controller
 * I2C Controller
 * System Control Unit (SCU)
 * SRAM mapping
 * X-DMA Controller (basic interface)
 * Static Memory Controller (SMC or FMC) - Only SPI Flash support
 * SPI Memory Controller
 * USB 2.0 Controller
 * SD/MMC storage controllers
 * SDRAM controller (dummy interface for basic settings and training)
 * Watchdog Controller
 * GPIO Controller (Master only)
 * UART
 * Ethernet controllers
 * Front LEDs (PCA9552 on I2C bus)
 * LPC Peripheral Controller (a subset of subdevices are supported)
 * Hash/Crypto Engine (HACE) - Hash support only. TODO: Crypto
 * ADC
 * eMMC Boot Controller (dummy)
 * PECI Controller (minimal)
 * I3C Controller
 * Internal Bridge Controller (SLI dummy)

Missing devices
---------------
 * PWM and Fan Controller
 * Slave GPIO Controller
 * Super I/O Controller
 * PCI-Express 1 Controller
 * Graphic Display Controller
 * MCTP Controller
 * Mailbox Controller
 * Virtual UART
 * eSPI Controller

Boot options
------------

Images can be downloaded from the ASPEED Forked OpenBMC GitHub release repository :

   https://github.com/AspeedTech-BMC/openbmc/releases

Booting the ast2700-evb machine
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Boot the AST2700 machine from the flash image.

There are two supported methods for booting the AST2700 machine with a flash image:

Manual boot using ``-device loader``:

It causes all 4 CPU cores to start execution from address ``0x430000000``, which
corresponds to the BL31 image load address.

.. code-block:: bash

  IMGDIR=ast2700-default
  UBOOT_SIZE=$(stat --format=%s -L ${IMGDIR}/u-boot-nodtb.bin)

  $ qemu-system-aarch64 -M ast2700-evb \
       -device loader,force-raw=on,addr=0x400000000,file=${IMGDIR}/u-boot-nodtb.bin \
       -device loader,force-raw=on,addr=$((0x400000000 + ${UBOOT_SIZE})),file=${IMGDIR}/u-boot.dtb \
       -device loader,force-raw=on,addr=0x430000000,file=${IMGDIR}/bl31.bin \
       -device loader,force-raw=on,addr=0x430080000,file=${IMGDIR}/optee/tee-raw.bin \
       -device loader,cpu-num=0,addr=0x430000000 \
       -device loader,cpu-num=1,addr=0x430000000 \
       -device loader,cpu-num=2,addr=0x430000000 \
       -device loader,cpu-num=3,addr=0x430000000 \
       -smp 4 \
       -drive file=${IMGDIR}/image-bmc,format=raw,if=mtd \
       -nographic

Boot using a virtual boot ROM (``-bios``):

If users do not specify the ``-bios option``, QEMU will attempt to load the
default vbootrom image ``ast27x0_bootrom.bin`` from either the current working
directory or the ``pc-bios`` directory within the QEMU source tree.

.. code-block:: bash

  $ qemu-system-aarch64 -M ast2700-evb \
      -drive file=image-bmc,format=raw,if=mtd \
      -nographic

The ``-bios`` option allows users to specify a custom path for the vbootrom
image to be loaded during boot. This will load the vbootrom image from the
specified path in the ${HOME} directory.

.. code-block:: bash

  -bios ${HOME}/ast27x0_bootrom.bin

Booting the ast2700fc machine
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

AST2700 features four Cortex-A35 primary processors and two Cortex-M4 coprocessors.
**ast2700-evb** machine focuses on emulating the four Cortex-A35 primary processors,
**ast2700fc** machine extends **ast2700-evb** by adding support for the two Cortex-M4 coprocessors.

Steps to boot the AST2700fc machine:

1. Ensure you have the following AST2700A1 binaries available in a directory

 * u-boot-nodtb.bin
 * u-boot.dtb
 * bl31.bin
 * optee/tee-raw.bin
 * image-bmc
 * zephyr-aspeed-ssp.elf (for SSP firmware, CPU 5)
 * zephyr-aspeed-tsp.elf (for TSP firmware, CPU 6)

2. Execute the following command to start ``ast2700fc`` machine:

.. code-block:: bash

  IMGDIR=ast2700-default
  UBOOT_SIZE=$(stat --format=%s -L ${IMGDIR}/u-boot-nodtb.bin)

  $ qemu-system-aarch64 -M ast2700fc \
       -device loader,force-raw=on,addr=0x400000000,file=${IMGDIR}/u-boot-nodtb.bin \
       -device loader,force-raw=on,addr=$((0x400000000 + ${UBOOT_SIZE})),file=${IMGDIR}/u-boot.dtb \
       -device loader,force-raw=on,addr=0x430000000,file=${IMGDIR}/bl31.bin \
       -device loader,force-raw=on,addr=0x430080000,file=${IMGDIR}/optee/tee-raw.bin \
       -device loader,cpu-num=0,addr=0x430000000 \
       -device loader,cpu-num=1,addr=0x430000000 \
       -device loader,cpu-num=2,addr=0x430000000 \
       -device loader,cpu-num=3,addr=0x430000000 \
       -drive file=${IMGDIR}/image-bmc,if=mtd,format=raw \
       -device loader,file=${IMGDIR}/zephyr-aspeed-ssp.elf,cpu-num=4 \
       -device loader,file=${IMGDIR}/zephyr-aspeed-tsp.elf,cpu-num=5 \
       -serial pty -serial pty -serial pty \
       -snapshot \
       -S -nographic

After launching QEMU, serial devices will be automatically redirected.
Example output:

.. code-block:: bash

   char device redirected to /dev/pts/55 (label serial0)
   char device redirected to /dev/pts/56 (label serial1)
   char device redirected to /dev/pts/57 (label serial2)

- serial0: Console for the four Cortex-A35 primary processors.
- serial1 and serial2: Consoles for the two Cortex-M4 coprocessors.

Use ``tio`` or another terminal emulator to connect to the consoles:

.. code-block:: bash

   $ tio /dev/pts/55
   $ tio /dev/pts/56
   $ tio /dev/pts/57


Aspeed minibmc family boards (``ast1030-evb``)
==================================================================

The QEMU Aspeed machines model mini BMCs of various Aspeed evaluation
boards. They are based on different releases of the
Aspeed SoC : the AST1030 integrating an ARM Cortex M4F CPU (200MHz).

The SoC comes with SRAM, SPI, I2C, etc.

AST1030 SoC based machines :

- ``ast1030-evb``          Aspeed AST1030 Evaluation board (Cortex-M4F)

Supported devices
-----------------

 * SMP (for the AST1030 Cortex-M4F)
 * Interrupt Controller (VIC)
 * Timer Controller
 * I2C Controller
 * System Control Unit (SCU)
 * SRAM mapping
 * Static Memory Controller (SMC or FMC) - Only SPI Flash support
 * SPI Memory Controller
 * USB 2.0 Controller
 * Watchdog Controller
 * GPIO Controller (Master only)
 * UART
 * LPC Peripheral Controller (a subset of subdevices are supported)
 * Hash/Crypto Engine (HACE) - Hash support only. TODO: HMAC and RSA
 * ADC
 * Secure Boot Controller
 * PECI Controller (minimal)


Missing devices
---------------

 * PWM and Fan Controller
 * Slave GPIO Controller
 * Mailbox Controller
 * Virtual UART
 * eSPI Controller
 * I3C Controller

Boot options
------------

The Aspeed machines can be started using the ``-kernel`` to load a
Zephyr OS or from a firmware. Images can be downloaded from the
ASPEED GitHub release repository :

   https://github.com/AspeedTech-BMC/zephyr/releases

To boot a kernel directly from a Zephyr build tree:

.. code-block:: bash

  $ qemu-system-arm -M ast1030-evb -nographic \
        -kernel zephyr.elf
