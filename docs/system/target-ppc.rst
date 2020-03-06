.. _PowerPC-System-emulator:

PowerPC System emulator
-----------------------

Use the executable ``qemu-system-ppc`` to simulate a complete 40P (PREP)
or PowerMac PowerPC system.

QEMU emulates the following PowerMac peripherals:

-  UniNorth or Grackle PCI Bridge

-  PCI VGA compatible card with VESA Bochs Extensions

-  2 PMAC IDE interfaces with hard disk and CD-ROM support

-  NE2000 PCI adapters

-  Non Volatile RAM

-  VIA-CUDA with ADB keyboard and mouse.

QEMU emulates the following 40P (PREP) peripherals:

-  PCI Bridge

-  PCI VGA compatible card with VESA Bochs Extensions

-  2 IDE interfaces with hard disk and CD-ROM support

-  Floppy disk

-  PCnet network adapters

-  Serial port

-  PREP Non Volatile RAM

-  PC compatible keyboard and mouse.

Since version 0.9.1, QEMU uses OpenBIOS https://www.openbios.org/ for
the g3beige and mac99 PowerMac and the 40p machines. OpenBIOS is a free
(GPL v2) portable firmware implementation. The goal is to implement a
100% IEEE 1275-1994 (referred to as Open Firmware) compliant firmware.

More information is available at
http://perso.magic.fr/l_indien/qemu-ppc/.
