Arm Server Base System Architecture Reference board (``sbsa-ref``)
==================================================================

While the ``virt`` board is a generic board platform that doesn't match
any real hardware the ``sbsa-ref`` board intends to look like real
hardware. The `Server Base System Architecture
<https://developer.arm.com/documentation/den0029/latest>`_ defines a
minimum base line of hardware support and importantly how the firmware
reports that to any operating system. It is a static system that
reports a very minimal DT to the firmware for non-discoverable
information about components affected by the qemu command line (i.e.
cpus and memory). As a result it must have a firmware specifically
built to expect a certain hardware layout (as you would in a real
machine).

It is intended to be a machine for developing firmware and testing
standards compliance with operating systems.

Supported devices
"""""""""""""""""

The sbsa-ref board supports:

  - A configurable number of AArch64 CPUs
  - GIC version 3
  - System bus AHCI controller
  - System bus EHCI controller
  - CDROM and hard disc on AHCI bus
  - E1000E ethernet card on PCIe bus
  - VGA display adaptor on PCIe bus
  - A generic SBSA watchdog device

