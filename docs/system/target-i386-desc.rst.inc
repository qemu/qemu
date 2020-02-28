The QEMU PC System emulator simulates the following peripherals:

-  i440FX host PCI bridge and PIIX3 PCI to ISA bridge

-  Cirrus CLGD 5446 PCI VGA card or dummy VGA card with Bochs VESA
   extensions (hardware level, including all non standard modes).

-  PS/2 mouse and keyboard

-  2 PCI IDE interfaces with hard disk and CD-ROM support

-  Floppy disk

-  PCI and ISA network adapters

-  Serial ports

-  IPMI BMC, either and internal or external one

-  Creative SoundBlaster 16 sound card

-  ENSONIQ AudioPCI ES1370 sound card

-  Intel 82801AA AC97 Audio compatible sound card

-  Intel HD Audio Controller and HDA codec

-  Adlib (OPL2) - Yamaha YM3812 compatible chip

-  Gravis Ultrasound GF1 sound card

-  CS4231A compatible sound card

-  PCI UHCI, OHCI, EHCI or XHCI USB controller and a virtual USB-1.1
   hub.

SMP is supported with up to 255 CPUs.

QEMU uses the PC BIOS from the Seabios project and the Plex86/Bochs LGPL
VGA BIOS.

QEMU uses YM3812 emulation by Tatsuyuki Satoh.

QEMU uses GUS emulation (GUSEMU32 http://www.deinmeister.de/gusemu/) by
Tibor \"TS\" Schütz.

Note that, by default, GUS shares IRQ(7) with parallel ports and so QEMU
must be told to not have parallel ports to have working GUS.

.. parsed-literal::

   |qemu_system_x86| dos.img -soundhw gus -parallel none

Alternatively:

.. parsed-literal::

   |qemu_system_x86| dos.img -device gus,irq=5

Or some other unclaimed IRQ.

CS4231A is the chip used in Windows Sound System and GUSMAX products
