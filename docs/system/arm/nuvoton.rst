Nuvoton iBMC boards (``kudo-bmc``, ``mori-bmc``, ``npcm750-evb``, ``quanta-gbs-bmc``, ``quanta-gsj``, ``npcm845-evb``)
======================================================================================================================

The `Nuvoton iBMC`_ chips are a family of Arm-based SoCs that are
designed to be used as Baseboard Management Controllers (BMCs) in various
servers. Currently there are two families: NPCM7XX series and
NPCM8XX series. NPCM7XX series feature one or two Arm Cortex-A9 CPU cores,
while NPCM8XX feature 4 Arm Cortex-A35 CPU cores. Both series contain a
different assortment of peripherals targeted for either Enterprise or Data
Center / Hyperscale applications.

.. _Nuvoton iBMC: https://www.nuvoton.com/products/cloud-computing/ibmc/

The NPCM750 SoC has two Cortex-A9 cores and is targeted for the Enterprise
segment. The following machines are based on this chip :

- ``npcm750-evb``       Nuvoton NPCM750 Evaluation board

The NPCM730 SoC has two Cortex-A9 cores and is targeted for Data Center and
Hyperscale applications. The following machines are based on this chip :

- ``quanta-gbs-bmc``    Quanta GBS server BMC
- ``quanta-gsj``        Quanta GSJ server BMC
- ``kudo-bmc``          Fii USA Kudo server BMC
- ``mori-bmc``          Fii USA Mori server BMC

There are also two more SoCs, NPCM710 and NPCM705, which are single-core
variants of NPCM750 and NPCM730, respectively. These are currently not
supported by QEMU.

The NPCM8xx SoC is the successor of the NPCM7xx SoC. It has 4 Cortex-A35 cores.
The following machines are based on this chip :

- ``npcm845-evb``       Nuvoton NPCM845 Evaluation board

Supported devices
-----------------

 * SMP (Dual Core Cortex-A9)
 * Cortex-A9MPCore built-in peripherals: SCU, GIC, Global Timer, Private Timer
   and Watchdog.
 * SRAM, ROM and DRAM mappings
 * System Global Control Registers (GCR)
 * Clock and reset controller (CLK)
 * Timer controller (TIM)
 * Serial ports (16550-based)
 * DDR4 memory controller (dummy interface indicating memory training is done)
 * OTP controllers (no protection features)
 * Flash Interface Unit (FIU; no protection features)
 * Random Number Generator (RNG)
 * USB host (USBH)
 * GPIO controller
 * Analog to Digital Converter (ADC)
 * Pulse Width Modulation (PWM)
 * SMBus controller (SMBF)
 * Ethernet controller (EMC)
 * Tachometer
 * Peripheral SPI controller (PSPI)

Missing devices
---------------

 * LPC/eSPI host-to-BMC interface, including

   * Keyboard and mouse controller interface (KBCI)
   * Keyboard Controller Style (KCS) channels
   * BIOS POST code FIFO
   * System Wake-up Control (SWC)
   * Shared memory (SHM)
   * eSPI slave interface
   * Block-transfer interface (8XX only)
   * Virtual UART (8XX only)

 * Ethernet controller (GMAC)
 * USB device (USBD)
 * SD/MMC host
 * PECI interface
 * PCI and PCIe root complex and bridges
 * VDM and MCTP support
 * Serial I/O expansion
 * LPC/eSPI host
 * Coprocessor
 * Graphics
 * Video capture
 * Encoding compression engine
 * Security features
 * I3C buses (8XX only)
 * Temperature sensor interface (8XX only)
 * Virtual UART (8XX only)
 * Flash monitor (8XX only)
 * JTAG master (8XX only)

Boot options
------------

The Nuvoton machines can boot from an OpenBMC firmware image, or directly into
a kernel using the ``-kernel`` option. OpenBMC machine names do not always
match QEMU machine names. Check the OpenBMC supported-machine list and Jenkins
for currently available source and pre-built images.

Known OpenBMC (v2.18.0) target names for QEMU Nuvoton machines per:

   https://github.com/openbmc/openbmc/blob/2.18.0/meta-phosphor/docs/supported-machines.md

.. list-table::
   :header-rows: 1

   * - QEMU machine
     - OpenBMC machine
   * - ``npcm750-evb``
     - ``evb-npcm750``
   * - ``npcm845-evb``
     - ``evb-npcm845``
   * - ``quanta-gbs-bmc``
     - ``gbs``
   * - ``kudo-bmc``
     - ``kudo``
   * - ``mori-bmc``
     - ``mori``

As of June 2026, the latest OpenBMC release, ``2.18.0``, no longer lists a
``gsj`` machine. To build an image for QEMU's ``quanta-gsj`` machine, use an
older OpenBMC release that still contains ``meta-quanta/meta-gsj``. The
``2.14.0`` release contains the ``gsj`` machine:

   https://github.com/openbmc/openbmc/tree/2.14.0/meta-quanta/meta-gsj

Some pre-built OpenBMC images for QEMU Nuvoton machines may be available on
Jenkins:

   https://jenkins.openbmc.org/

To find a pre-built MTD image on Jenkins, start from the Jenkins home page and
open the ``latest-master`` job. Select the matrix configuration whose
``target`` matches the OpenBMC machine name, for example
``label=docker-builder,target=gbs``, then open its latest successful build's
artifacts. The MTD image is usually published under
``openbmc/build/tmp/deploy/images/<machine>/`` as
``obmc-phosphor-image-<machine>-<timestamp>.static.mtd``. If Jenkins does not
list a matching target, or the build artifacts do not include an MTD image,
there is no current pre-built MTD image for that machine.

The firmware image should be attached as an MTD drive. Example:

.. code-block:: bash

  $ qemu-system-arm -machine quanta-gbs-bmc -nographic \
      -drive file=obmc-phosphor-image-gbs-xxxxxx.static.mtd,if=mtd,bus=0,unit=0,format=raw

The default root password for test images is usually ``0penBmc``.

For other machines that don't have pre-built images on Jenkins, build an image
from source by following the OpenBMC build documentation.
