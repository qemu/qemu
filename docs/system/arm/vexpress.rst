Arm Versatile Express boards (``vexpress-a9``, ``vexpress-a15``)
================================================================

QEMU models two variants of the Arm Versatile Express development
board family:

- ``vexpress-a9`` models the combination of the Versatile Express
  motherboard and the CoreTile Express A9x4 daughterboard
- ``vexpress-a15`` models the combination of the Versatile Express
  motherboard and the CoreTile Express A15x2 daughterboard

Note that as this hardware does not have PCI, IDE or SCSI,
the only available storage option is emulated SD card.

Implemented devices:

- PL041 audio
- PL181 SD controller
- PL050 keyboard and mouse
- PL011 UARTs
- SP804 timers
- I2C controller
- PL031 RTC
- PL111 LCD display controller
- Flash memory
- LAN9118 ethernet

Unimplemented devices:

- SP810 system control block
- PCI-express
- USB controller (Philips ISP1761)
- Local DAP ROM
- CoreSight interfaces
- PL301 AXI interconnect
- SCC
- System counter
- HDLCD controller (``vexpress-a15``)
- SP805 watchdog
- PL341 dynamic memory controller
- DMA330 DMA controller
- PL354 static memory controller
- BP147 TrustZone Protection Controller
- TrustZone Address Space Controller

Other differences between the hardware and the QEMU model:

- QEMU will default to creating one CPU unless you pass a different
  ``-smp`` argument
- QEMU allows the amount of RAM provided to be specified with the
  ``-m`` argument
- QEMU defaults to providing a CPU which does not provide either
  TrustZone or the Virtualization Extensions: if you want these you
  must enable them with ``-machine secure=on`` and ``-machine
  virtualization=on``
- QEMU provides 4 virtio-mmio virtio transports; these start at
  address ``0x10013000`` for ``vexpress-a9`` and at ``0x1c130000`` for
  ``vexpress-a15``, and have IRQs from 40 upwards. If a dtb is
  provided on the command line then QEMU will edit it to include
  suitable entries describing these transports for the guest.
- QEMU does not currently support either dynamic or static remapping
  of the area of memory at address 0: it is always mapped to alias
  the first flash bank

Booting a Linux kernel
----------------------

Building a current Linux kernel with ``multi_v7_defconfig`` should be
enough to get something running. Nowadays an out-of-tree build is
recommended (and also useful if you build a lot of different targets).
In the following example $BLD points to the build directory and $SRC
points to the root of the Linux source tree. You can drop $SRC if you
are running from there.

.. code-block:: bash

  $ make O=$BLD -C $SRC ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- multi_v7_defconfig
  $ make O=$BLD -C $SRC ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-

By default you will want to boot your rootfs off the sdcard interface.
Your rootfs will need to be padded to the right size. With a suitable
DTB you could also add devices to the virtio-mmio bus.

.. code-block:: bash

  $ qemu-system-arm -cpu cortex-a15 -smp 4 -m 4096 \
      -machine type=vexpress-a15 -serial mon:stdio \
      -drive if=sd,driver=file,filename=armel-rootfs.ext4 \
      -kernel zImage  \
      -dtb vexpress-v2p-ca15-tc1.dtb \
      -append "console=ttyAMA0 root=/dev/mmcblk0 ro"
