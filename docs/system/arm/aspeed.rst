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
- ``supermicrox11-bmc``    Supermicro X11 BMC

AST2500 SoC based machines :

- ``ast2500-evb``          Aspeed AST2500 Evaluation board
- ``romulus-bmc``          OpenPOWER Romulus POWER9 BMC
- ``witherspoon-bmc``      OpenPOWER Witherspoon POWER9 BMC
- ``sonorapass-bmc``       OCP SonoraPass BMC
- ``fp5280g2-bmc``         Inspur FP5280G2 BMC
- ``g220a-bmc``            Bytedance G220A BMC

AST2600 SoC based machines :

- ``ast2600-evb``          Aspeed AST2600 Evaluation board (Cortex-A7)
- ``tacoma-bmc``           OpenPOWER Witherspoon POWER9 AST2600 BMC
- ``rainier-bmc``          IBM Rainier POWER10 BMC
- ``fuji-bmc``             Facebook Fuji BMC
- ``bletchley-bmc``        Facebook Bletchley BMC
- ``fby35-bmc``            Facebook fby35 BMC
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

To boot a kernel directly from a Linux build tree:

.. code-block:: bash

  $ qemu-system-arm -M ast2600-evb -nographic \
        -kernel arch/arm/boot/zImage \
        -dtb arch/arm/boot/dts/aspeed-ast2600-evb.dtb \
        -initrd rootfs.cpio

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

Facebook Yosemite v3.5 Platform and CraterLake Server (``fby35``)
==================================================================

Facebook has a series of multi-node compute server designs named
Yosemite. The most recent version released was
`Yosemite v3 <https://www.opencompute.org/documents/ocp-yosemite-v3-platform-design-specification-1v16-pdf>`__.

Yosemite v3.5 is an iteration on this design, and is very similar: there's a
baseboard with a BMC, and 4 server slots. The new server board design termed
"CraterLake" includes a Bridge IC (BIC), with room for expansion boards to
include various compute accelerators (video, inferencing, etc). At the moment,
only the first server slot's BIC is included.

Yosemite v3.5 is itself a sled which fits into a 40U chassis, and 3 sleds
can be fit into a chassis. See `here <https://www.opencompute.org/products/423/wiwynn-yosemite-v3-server>`__
for an example.

In this generation, the BMC is an AST2600 and each BIC is an AST1030. The BMC
runs `OpenBMC <https://github.com/facebook/openbmc>`__, and the BIC runs
`OpenBIC <https://github.com/facebook/openbic>`__.

Firmware images can be retrieved from the Github releases or built from the
source code, see the README's for instructions on that. This image uses the
"fby35" machine recipe from OpenBMC, and the "yv35-cl" target from OpenBIC.
Some reference images can also be found here:

.. code-block:: bash

    $ wget https://github.com/facebook/openbmc/releases/download/openbmc-e2294ff5d31d/fby35.mtd
    $ wget https://github.com/peterdelevoryas/OpenBIC/releases/download/oby35-cl-2022.13.01/Y35BCL.elf

Since this machine has multiple SoC's, each with their own serial console, the
recommended way to run it is to allocate a pseudoterminal for each serial
console and let the monitor use stdio. Also, starting in a paused state is
useful because it allows you to attach to the pseudoterminals before the boot
process starts.

.. code-block:: bash

    $ qemu-system-arm -machine fby35 \
        -drive file=fby35.mtd,format=raw,if=mtd \
        -device loader,file=Y35BCL.elf,addr=0,cpu-num=2 \
        -serial pty -serial pty -serial mon:stdio \
        -display none -S
    $ screen /dev/tty0 # In a separate TMUX pane, terminal window, etc.
    $ screen /dev/tty1
    $ (qemu) c		   # Start the boot process once screen is setup.
