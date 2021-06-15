Aspeed family boards (``*-bmc``, ``ast2500-evb``, ``ast2600-evb``)
==================================================================

The QEMU Aspeed machines model BMCs of various OpenPOWER systems and
Aspeed evaluation boards. They are based on different releases of the
Aspeed SoC : the AST2400 integrating an ARM926EJ-S CPU (400MHz), the
AST2500 with an ARM1176JZS CPU (800MHz) and more recently the AST2600
with dual cores ARM Cortex-A7 CPUs (1.2GHz).

The SoC comes with RAM, Gigabit ethernet, USB, SD/MMC, USB, SPI, I2C,
etc.

AST2400 SoC based machines :

- ``palmetto-bmc``         OpenPOWER Palmetto POWER8 BMC
- ``quanta-q71l-bmc``      OpenBMC Quanta BMC

AST2500 SoC based machines :

- ``ast2500-evb``          Aspeed AST2500 Evaluation board
- ``romulus-bmc``          OpenPOWER Romulus POWER9 BMC
- ``witherspoon-bmc``      OpenPOWER Witherspoon POWER9 BMC
- ``sonorapass-bmc``       OCP SonoraPass BMC
- ``swift-bmc``            OpenPOWER Swift BMC POWER9

AST2600 SoC based machines :

- ``ast2600-evb``          Aspeed AST2600 Evaluation board (Cortex-A7)
- ``tacoma-bmc``           OpenPOWER Witherspoon POWER9 AST2600 BMC

Supported devices
-----------------

 * SMP (for the AST2600 Cortex-A7)
 * Interrupt Controller (VIC)
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
 * Hash/Crypto Engine (HACE) - Hash support only. TODO: HMAC and RSA


Missing devices
---------------

 * Coprocessor support
 * ADC (out of tree implementation)
 * PWM and Fan Controller
 * Slave GPIO Controller
 * Super I/O Controller
 * PCI-Express 1 Controller
 * Graphic Display Controller
 * PECI Controller
 * MCTP Controller
 * Mailbox Controller
 * Virtual UART
 * eSPI Controller
 * I3C Controller

Boot options
------------

The Aspeed machines can be started using the ``-kernel`` option to
load a Linux kernel or from a firmware. Images can be downloaded from
the OpenBMC jenkins :

   https://jenkins.openbmc.org/job/ci-openbmc/lastSuccessfulBuild/distro=ubuntu,label=docker-builder

or directly from the OpenBMC GitHub release repository :

   https://github.com/openbmc/openbmc/releases

The image should be attached as an MTD drive. Run :

.. code-block:: bash

  $ qemu-system-arm -M romulus-bmc -nic user \
	-drive file=obmc-phosphor-image-romulus.static.mtd,format=raw,if=mtd -nographic

Options specific to Aspeed machines are :

 * ``execute-in-place`` which emulates the boot from the CE0 flash
   device by using the FMC controller to load the instructions, and
   not simply from RAM. This takes a little longer.

 * ``fmc-model`` to change the FMC Flash model. FW needs support for
   the chip model to boot.

 * ``spi-model`` to change the SPI Flash model.

For instance, to start the ``ast2500-evb`` machine with a different
FMC chip and a bigger (64M) SPI chip, use :

.. code-block:: bash

  -M ast2500-evb,fmc-model=mx25l25635e,spi-model=mx66u51235f
