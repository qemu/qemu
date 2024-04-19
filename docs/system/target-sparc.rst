.. _Sparc32-System-emulator:

Sparc32 System emulator
-----------------------

Use the executable ``qemu-system-sparc`` to simulate the following Sun4m
architecture machines:

-  SPARCstation 4

-  SPARCstation 5

-  SPARCstation 10

-  SPARCstation 20

-  SPARCserver 600MP

-  SPARCstation LX

-  SPARCstation Voyager

-  SPARCclassic

-  SPARCbook

The emulation is somewhat complete. SMP up to 16 CPUs is supported, but
Linux limits the number of usable CPUs to 4.

The list of available CPUs can be viewed by starting QEMU with ``-cpu help``.
Optional boolean features can be added with a "+" in front of the feature name,
or disabled with a "-" in front of the name, for example
``-cpu TI-SuperSparc-II,+float128``.

QEMU emulates the following sun4m peripherals:

-  IOMMU

-  TCX or cgthree Frame buffer

-  Lance (Am7990) Ethernet

-  Non Volatile RAM M48T02/M48T08

-  Slave I/O: timers, interrupt controllers, Zilog serial ports,
   :ref:`keyboard` and power/reset logic

-  ESP SCSI controller with hard disk and CD-ROM support

-  Floppy drive (not on SS-600MP)

-  CS4231 sound device (only on SS-5, not working yet)

The number of peripherals is fixed in the architecture. Maximum memory
size depends on the machine type, for SS-5 it is 256MB and for others
2047MB.

Since version 0.8.2, QEMU uses OpenBIOS https://www.openbios.org/.
OpenBIOS is a free (GPL v2) portable firmware implementation. The goal
is to implement a 100% IEEE 1275-1994 (referred to as Open Firmware)
compliant firmware.

Please note that currently older Solaris kernels don't work; this is probably
due to interface issues between OpenBIOS and Solaris.
