
EDU device
==========

..
   Copyright (c) 2014-2015 Jiri Slaby

   This document is licensed under the GPLv2 (or later).

This is an educational device for writing (kernel) drivers. Its original
intention was to support the Linux kernel lectures taught at the Masaryk
University. Students are given this virtual device and are expected to write a
driver with I/Os, IRQs, DMAs and such.

The devices behaves very similar to the PCI bridge present in the COMBO6 cards
developed under the Liberouter wings. Both PCI device ID and PCI space is
inherited from that device.

Command line switches
---------------------

``-device edu[,dma_mask=mask]``
    ``dma_mask`` makes the virtual device work with DMA addresses with the given
    mask. For educational purposes, the device supports only 28 bits (256 MiB)
    by default. Students shall set dma_mask for the device in the OS driver
    properly.

PCI specs
---------

PCI ID:
   ``1234:11e8``

PCI Region 0:
   I/O memory, 1 MB in size. Users are supposed to communicate with the card
   through this memory.

MMIO area spec
--------------

Only ``size == 4`` accesses are allowed for addresses ``< 0x80``.
``size == 4`` or ``size == 8`` for the rest.

0x00 (RO) : identification
            Value is in the form ``0xRRrr00edu`` where:
	    - ``RR`` -- major version
	    - ``rr`` -- minor version

0x04 (RW) : card liveness check
	    It is a simple value inversion (``~`` C operator).

0x08 (RW) : factorial computation
	    The stored value is taken and factorial of it is put back here.
	    This happens only after factorial bit in the status register (0x20
	    below) is cleared.

0x20 (RW) : status register
            Bitwise OR of:

            0x01
              computing factorial (RO)
	    0x80
              raise interrupt after finishing factorial computation

0x24 (RO) : interrupt status register
	    It contains values which raised the interrupt (see interrupt raise
	    register below).

0x60 (WO) : interrupt raise register
	    Raise an interrupt. The value will be put to the interrupt status
	    register (using bitwise OR).

0x64 (WO) : interrupt acknowledge register
	    Clear an interrupt. The value will be cleared from the interrupt
	    status register. This needs to be done from the ISR to stop
	    generating interrupts.

0x80 (RW) : DMA source address
	    Where to perform the DMA from.

0x88 (RW) : DMA destination address
	    Where to perform the DMA to.

0x90 (RW) : DMA transfer count
	    The size of the area to perform the DMA on.

0x98 (RW) : DMA command register
            Bitwise OR of:

            0x01
              start transfer
	    0x02
              direction (0: from RAM to EDU, 1: from EDU to RAM)
	    0x04
              raise interrupt 0x100 after finishing the DMA

IRQ controller
--------------

An IRQ is generated when written to the interrupt raise register. The value
appears in interrupt status register when the interrupt is raised and has to
be written to the interrupt acknowledge register to lower it.

The device supports both INTx and MSI interrupt. By default, INTx is
used. Even if the driver disabled INTx and only uses MSI, it still
needs to update the acknowledge register at the end of the IRQ handler
routine.

DMA controller
--------------

One has to specify, source, destination, size, and start the transfer. One
4096 bytes long buffer at offset 0x40000 is available in the EDU device. I.e.
one can perform DMA to/from this space when programmed properly.

Example of transferring a 100 byte block to and from the buffer using a given
PCI address ``addr``:

::

  addr     -> DMA source address
  0x40000  -> DMA destination address
  100      -> DMA transfer count
  1        -> DMA command register
  while (DMA command register & 1)
      ;

::

  0x40000  -> DMA source address
  addr+100 -> DMA destination address
  100      -> DMA transfer count
  3        -> DMA command register
  while (DMA command register & 1)
      ;
