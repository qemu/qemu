Arm Versatile boards (``versatileab``, ``versatilepb``)
=======================================================

The Arm Versatile baseboard is emulated with the following devices:

-  ARM926E, ARM1136 or Cortex-A8 CPU

-  PL190 Vectored Interrupt Controller

-  Four PL011 UARTs

-  SMC 91c111 Ethernet adapter

-  PL110 LCD controller

-  PL050 KMI with PS/2 keyboard and mouse.

-  PCI host bridge. Note the emulated PCI bridge only provides access
   to PCI memory space. It does not provide access to PCI IO space. This
   means some devices (eg. ne2k_pci NIC) are not usable, and others (eg.
   rtl8139 NIC) are only usable when the guest drivers use the memory
   mapped control registers.

-  PCI OHCI USB controller.

-  LSI53C895A PCI SCSI Host Bus Adapter with hard disk and CD-ROM
   devices.

-  PL181 MultiMedia Card Interface with SD card.
