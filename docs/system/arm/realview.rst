Arm Realview boards (``realview-eb``, ``realview-eb-mpcore``, ``realview-pb-a8``, ``realview-pbx-a9``)
======================================================================================================

Several variants of the Arm RealView baseboard are emulated, including
the EB, PB-A8 and PBX-A9. Due to interactions with the bootloader, only
certain Linux kernel configurations work out of the box on these boards.

Kernels for the PB-A8 board should have CONFIG_REALVIEW_HIGH_PHYS_OFFSET
enabled in the kernel, and expect 512M RAM. Kernels for The PBX-A9 board
should have CONFIG_SPARSEMEM enabled, CONFIG_REALVIEW_HIGH_PHYS_OFFSET
disabled and expect 1024M RAM.

The following devices are emulated:

-  ARM926E, ARM1136, ARM11MPCore, Cortex-A8 or Cortex-A9 MPCore CPU

-  Arm AMBA Generic/Distributed Interrupt Controller

-  Four PL011 UARTs

-  SMC 91c111 or SMSC LAN9118 Ethernet adapter

-  PL110 LCD controller

-  PL050 KMI with PS/2 keyboard and mouse

-  PCI host bridge

-  PCI OHCI USB controller

-  LSI53C895A PCI SCSI Host Bus Adapter with hard disk and CD-ROM
   devices

-  PL181 MultiMedia Card Interface with SD card.
