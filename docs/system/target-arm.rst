.. _ARM-System-emulator:

ARM System emulator
-------------------

Use the executable ``qemu-system-arm`` to simulate a ARM machine. The
ARM Integrator/CP board is emulated with the following devices:

-  ARM926E, ARM1026E, ARM946E, ARM1136 or Cortex-A8 CPU

-  Two PL011 UARTs

-  SMC 91c111 Ethernet adapter

-  PL110 LCD controller

-  PL050 KMI with PS/2 keyboard and mouse.

-  PL181 MultiMedia Card Interface with SD card.

The ARM Versatile baseboard is emulated with the following devices:

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

Several variants of the ARM RealView baseboard are emulated, including
the EB, PB-A8 and PBX-A9. Due to interactions with the bootloader, only
certain Linux kernel configurations work out of the box on these boards.

Kernels for the PB-A8 board should have CONFIG_REALVIEW_HIGH_PHYS_OFFSET
enabled in the kernel, and expect 512M RAM. Kernels for The PBX-A9 board
should have CONFIG_SPARSEMEM enabled, CONFIG_REALVIEW_HIGH_PHYS_OFFSET
disabled and expect 1024M RAM.

The following devices are emulated:

-  ARM926E, ARM1136, ARM11MPCore, Cortex-A8 or Cortex-A9 MPCore CPU

-  ARM AMBA Generic/Distributed Interrupt Controller

-  Four PL011 UARTs

-  SMC 91c111 or SMSC LAN9118 Ethernet adapter

-  PL110 LCD controller

-  PL050 KMI with PS/2 keyboard and mouse

-  PCI host bridge

-  PCI OHCI USB controller

-  LSI53C895A PCI SCSI Host Bus Adapter with hard disk and CD-ROM
   devices

-  PL181 MultiMedia Card Interface with SD card.

The XScale-based clamshell PDA models (\"Spitz\", \"Akita\", \"Borzoi\"
and \"Terrier\") emulation includes the following peripherals:

-  Intel PXA270 System-on-chip (ARM V5TE core)

-  NAND Flash memory

-  IBM/Hitachi DSCM microdrive in a PXA PCMCIA slot - not in \"Akita\"

-  On-chip OHCI USB controller

-  On-chip LCD controller

-  On-chip Real Time Clock

-  TI ADS7846 touchscreen controller on SSP bus

-  Maxim MAX1111 analog-digital converter on |I2C| bus

-  GPIO-connected keyboard controller and LEDs

-  Secure Digital card connected to PXA MMC/SD host

-  Three on-chip UARTs

-  WM8750 audio CODEC on |I2C| and |I2S| busses

The Palm Tungsten|E PDA (codename \"Cheetah\") emulation includes the
following elements:

-  Texas Instruments OMAP310 System-on-chip (ARM 925T core)

-  ROM and RAM memories (ROM firmware image can be loaded with
   -option-rom)

-  On-chip LCD controller

-  On-chip Real Time Clock

-  TI TSC2102i touchscreen controller / analog-digital converter /
   Audio CODEC, connected through MicroWire and |I2S| busses

-  GPIO-connected matrix keypad

-  Secure Digital card connected to OMAP MMC/SD host

-  Three on-chip UARTs

Nokia N800 and N810 internet tablets (known also as RX-34 and RX-44 /
48) emulation supports the following elements:

-  Texas Instruments OMAP2420 System-on-chip (ARM 1136 core)

-  RAM and non-volatile OneNAND Flash memories

-  Display connected to EPSON remote framebuffer chip and OMAP on-chip
   display controller and a LS041y3 MIPI DBI-C controller

-  TI TSC2301 (in N800) and TI TSC2005 (in N810) touchscreen
   controllers driven through SPI bus

-  National Semiconductor LM8323-controlled qwerty keyboard driven
   through |I2C| bus

-  Secure Digital card connected to OMAP MMC/SD host

-  Three OMAP on-chip UARTs and on-chip STI debugging console

-  Mentor Graphics \"Inventra\" dual-role USB controller embedded in a
   TI TUSB6010 chip - only USB host mode is supported

-  TI TMP105 temperature sensor driven through |I2C| bus

-  TI TWL92230C power management companion with an RTC on
   |I2C| bus

-  Nokia RETU and TAHVO multi-purpose chips with an RTC, connected
   through CBUS

The Luminary Micro Stellaris LM3S811EVB emulation includes the following
devices:

-  Cortex-M3 CPU core.

-  64k Flash and 8k SRAM.

-  Timers, UARTs, ADC and |I2C| interface.

-  OSRAM Pictiva 96x16 OLED with SSD0303 controller on
   |I2C| bus.

The Luminary Micro Stellaris LM3S6965EVB emulation includes the
following devices:

-  Cortex-M3 CPU core.

-  256k Flash and 64k SRAM.

-  Timers, UARTs, ADC, |I2C| and SSI interfaces.

-  OSRAM Pictiva 128x64 OLED with SSD0323 controller connected via
   SSI.

The Freecom MusicPal internet radio emulation includes the following
elements:

-  Marvell MV88W8618 ARM core.

-  32 MB RAM, 256 KB SRAM, 8 MB flash.

-  Up to 2 16550 UARTs

-  MV88W8xx8 Ethernet controller

-  MV88W8618 audio controller, WM8750 CODEC and mixer

-  128x64 display with brightness control

-  2 buttons, 2 navigation wheels with button function

The Siemens SX1 models v1 and v2 (default) basic emulation. The
emulation includes the following elements:

-  Texas Instruments OMAP310 System-on-chip (ARM 925T core)

-  ROM and RAM memories (ROM firmware image can be loaded with
   -pflash) V1 1 Flash of 16MB and 1 Flash of 8MB V2 1 Flash of 32MB

-  On-chip LCD controller

-  On-chip Real Time Clock

-  Secure Digital card connected to OMAP MMC/SD host

-  Three on-chip UARTs

A Linux 2.6 test image is available on the QEMU web site. More
information is available in the QEMU mailing-list archive.

The following options are specific to the ARM emulation:

``-semihosting``
   Enable semihosting syscall emulation.

   On ARM this implements the \"Angel\" interface.

   Note that this allows guest direct access to the host filesystem, so
   should only be used with trusted guest OS.
