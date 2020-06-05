Aspeed family boards (``*-bmc``, ``ast2500-evb``, ``ast2600-evb``)
==================================================================

The QEMU Aspeed machines model BMCs of various OpenPOWER systems and
Aspeed evaluation boards. They are based on different releases of the
Aspeed SoC : the AST2400 integrating an ARM926EJ-S CPU (400MHz), the
AST2500 with an ARM1176JZS CPU (800MHz) and more recently the AST2600
with dual cores ARM Cortex A7 CPUs (1.2GHz).

The SoC comes with RAM, Gigabit ethernet, USB, SD/MMC, USB, SPI, I2C,
etc.

AST2400 SoC based machines :

- ``palmetto-bmc``         OpenPOWER Palmetto POWER8 BMC

AST2500 SoC based machines :

- ``ast2500-evb``          Aspeed AST2500 Evaluation board
- ``romulus-bmc``          OpenPOWER Romulus POWER9 BMC
- ``witherspoon-bmc``      OpenPOWER Witherspoon POWER9 BMC
- ``sonorapass-bmc``       OCP SonoraPass BMC
- ``swift-bmc``            OpenPOWER Swift BMC POWER9

AST2600 SoC based machines :

- ``ast2600-evb``          Aspeed AST2600 Evaluation board (Cortex A7)
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


Missing devices
---------------

 * Coprocessor support
 * ADC (out of tree implementation)
 * PWM and Fan Controller
 * LPC Bus Controller
 * Slave GPIO Controller
 * Super I/O Controller
 * Hash/Crypto Engine
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

The Aspeed machines can be started using the -kernel option to load a
Linux kernel or from a firmare image which can be downloaded from the
OpenPOWER jenkins :

   https://openpower.xyz/

The image should be attached as an MTD drive. Run :

.. code-block:: bash

  $ qemu-system-arm -M romulus-bmc -nic user \
	-drive file=flash-romulus,format=raw,if=mtd -nographic
