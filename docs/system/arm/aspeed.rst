Aspeed family boards (``ast2500-evb``, ``ast2600-evb``, ``ast2700-evb``, ``bletchley-bmc``, ``fuji-bmc``, ``fby35-bmc``, ``fp5280g2-bmc``, ``g220a-bmc``, ``palmetto-bmc``, ``qcom-dc-scm-v1-bmc``, ``qcom-firework-bmc``, ``quanta-q71l-bmc``, ``rainier-bmc``, ``romulus-bmc``, ``sonorapass-bmc``, ``supermicrox11-bmc``, ``tiogapass-bmc``, ``tacoma-bmc``, ``witherspoon-bmc``, ``yosemitev2-bmc``)
========================================================================================================================================================================================================================================================================================================================================================================================================

The QEMU Aspeed machines model BMCs of various OpenPOWER systems and
Aspeed evaluation boards. They are based on different releases of the
Aspeed SoC : the AST2400 integrating an ARM926EJ-S CPU (400MHz), the
AST2500 with an ARM1176JZS CPU (800MHz), the AST2600
with dual cores ARM Cortex-A7 CPUs (1.2GHz) and more recently the AST2700
with quad cores ARM Cortex-A35 64 bits CPUs (1.6GHz)

The SoC comes with RAM, Gigabit ethernet, USB, SD/MMC, USB, SPI, I2C,
etc.

AST2400 SoC based machines :

- ``palmetto-bmc``         OpenPOWER Palmetto POWER8 BMC
- ``quanta-q71l-bmc``      OpenBMC Quanta BMC
- ``supermicrox11-bmc``    Supermicro X11 BMC

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
- ``tacoma-bmc``           OpenPOWER Witherspoon POWER9 AST2600 BMC
- ``rainier-bmc``          IBM Rainier POWER10 BMC
- ``fuji-bmc``             Facebook Fuji BMC
- ``bletchley-bmc``        Facebook Bletchley BMC
- ``fby35-bmc``            Facebook fby35 BMC
- ``qcom-dc-scm-v1-bmc``   Qualcomm DC-SCM V1 BMC
- ``qcom-firework-bmc``    Qualcomm Firework BMC

AST2700 SoC based machines :

- ``ast2700-evb``          Aspeed AST2700 Evaluation board (Cortex-A35)

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

To boot a kernel directly from a Linux build tree:

.. code-block:: bash

  $ qemu-system-arm -M ast2600-evb -nographic \
        -kernel arch/arm/boot/zImage \
        -dtb arch/arm/boot/dts/aspeed-ast2600-evb.dtb \
        -initrd rootfs.cpio

To boot the machine from the flash image, use an MTD drive :

.. code-block:: bash

  $ qemu-system-arm -M romulus-bmc -nic user \
	-drive file=obmc-phosphor-image-romulus.static.mtd,format=raw,if=mtd -nographic

Options specific to Aspeed machines are :

 * ``boot-emmc`` to set or unset boot from eMMC (AST2600).

 * ``execute-in-place`` which emulates the boot from the CE0 flash
   device by using the FMC controller to load the instructions, and
   not simply from RAM. This takes a little longer.

 * ``fmc-model`` to change the default FMC Flash model. FW needs
   support for the chip model to boot.

 * ``spi-model`` to change the default SPI Flash model.

 * ``bmc-console`` to change the default console device. Most of the
   machines use the ``UART5`` device for a boot console, which is
   mapped on ``/dev/ttyS4`` under Linux, but it is not always the
   case.

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

To change the boot console and use device ``UART3`` (``/dev/ttyS2``
under Linux), use :

.. code-block:: bash

  -M ast2500-evb,bmc-console=uart3


Boot the AST2700 machine from the flash image, use an MTD drive :

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
