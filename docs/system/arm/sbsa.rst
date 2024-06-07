Arm Server Base System Architecture Reference board (``sbsa-ref``)
==================================================================

The ``sbsa-ref`` board intends to look like real hardware (while the ``virt``
board is a generic board platform that doesn't match any real hardware).

The hardware part is defined by two specifications:

  - `Base System Architecture <https://developer.arm.com/documentation/den0094/>`__ (BSA)
  - `Server Base System Architecture <https://developer.arm.com/documentation/den0029/>`__ (SBSA)

The `Arm Base Boot Requirements <https://developer.arm.com/documentation/den0044/>`__ (BBR)
specification defines how the firmware reports that to any operating system.

It is intended to be a machine for developing firmware and testing
standards compliance with operating systems.

Supported devices
"""""""""""""""""

The ``sbsa-ref`` board supports:

  - A configurable number of AArch64 CPUs
  - GIC version 3
  - System bus AHCI controller
  - System bus XHCI controller
  - CDROM and hard disc on AHCI bus
  - E1000E ethernet card on PCIe bus
  - Bochs display adapter on PCIe bus
  - A generic SBSA watchdog device


Board to firmware interface
"""""""""""""""""""""""""""

``sbsa-ref`` is a static system that reports a very minimal devicetree to the
firmware for non-discoverable information about system components. This
includes both internal hardware and parts affected by the qemu command line
(i.e. CPUs and memory). As a result it must have a firmware specifically built
to expect a certain hardware layout (as you would in a real machine).

Note
''''

QEMU provides the guest EL3 firmware with minimal information about hardware
platform using minimalistic devicetree. This is not a Linux devicetree. It is
not even a firmware devicetree.

It is information passed from QEMU to describe the information a hardware
platform would have other mechanisms to discover at runtime, that are affected
by the QEMU command line.

Ultimately this devicetree may be replaced by IPC calls to an emulated SCP.

DeviceTree information
''''''''''''''''''''''

The devicetree reports:

   - CPUs
   - memory
   - platform version
   - GIC addresses
   - NUMA node id for CPUs and memory
   - CPU topology information

Platform version
''''''''''''''''

The platform version is only for informing platform firmware about
what kind of ``sbsa-ref`` board it is running on. It is neither
a QEMU versioned machine type nor a reflection of the level of the
SBSA/SystemReady SR support provided.

The ``machine-version-major`` value is updated when changes breaking
fw compatibility are introduced. The ``machine-version-minor`` value
is updated when features are added that don't break fw compatibility.

Platform version changes:

0.0
  Devicetree holds information about CPUs, memory and platform version.

0.1
  GIC information is present in devicetree.

0.2
  GIC ITS information is present in devicetree.

0.3
  The USB controller is an XHCI device, not EHCI.

0.4
  CPU topology information is present in devicetree.
