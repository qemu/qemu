=======================
QEMU PCI serial devices
=======================

QEMU implements some PCI serial devices which are simple PCI
wrappers around one or more 16550 UARTs.

There is one single-port variant and two multiport-variants.  Linux
guests work out-of-the box with all cards.  There is a Windows inf file
(``docs/qemupciserial.inf``) to set up the cards in Windows guests.


Single-port card
----------------

Name:
  ``pci-serial``
PCI ID:
  1b36:0002
PCI Region 0:
   IO bar, 8 bytes long, with the 16550 UART mapped to it.
Interrupt:
   Wired to pin A.


Multiport cards
---------------

Name:
  ``pci-serial-2x``, ``pci-serial-4x``
PCI ID:
  1b36:0003 (``-2x``) and 1b36:0004 (``-4x``)
PCI Region 0:
   IO bar, with two or four 16550 UARTs mapped after each other.
   The first is at offset 0, the second at offset 8, and so on.
Interrupt:
   Wired to pin A.
