.. _OpenRISC-System-emulator:

OpenRISC System emulator
~~~~~~~~~~~~~~~~~~~~~~~~

QEMU can emulate 32-bit OpenRISC CPUs using the ``qemu-system-or1k`` executable.

OpenRISC CPUs are generally built into "system-on-chip" (SoC) designs that run
on FPGAs.  These SoCs are based on the same core architecture as the or1ksim
(the original OpenRISC instruction level simulator) which QEMU supports. For
this reason QEMU does not need to support many different boards to support the
OpenRISC hardware ecosystem.

The OpenRISC CPU supported by QEMU is the ``or1200``, it supports an MMU and can
run linux.

Choosing a board model
======================

For QEMU's OpenRISC system emulation, you must specify which board model you
want to use with the ``-M`` or ``--machine`` option; the default machine is
``or1k-sim``.

If you intend to boot Linux, it is possible to have a single kernel image that
will boot on any of the QEMU machines. To do this one would compile all required
drivers into the kernel. This is possible because QEMU will create a device tree
structure that describes the QEMU machine and pass a pointer to the structure to
the kernel.  The kernel can then use this to configure itself for the machine.

However, typically users will have specific firmware images for a specific machine.

If you already have a system image or a kernel that works on hardware and you
want to boot with QEMU, check whether QEMU lists that machine in its ``-machine
help`` output. If it is listed, then you can probably use that board model. If
it is not listed, then unfortunately your image will almost certainly not boot
on QEMU. (You might be able to extract the filesystem and use that with a
different kernel which boots on a system that QEMU does emulate.)

If you don't care about reproducing the idiosyncrasies of a particular
bit of hardware, such as small amount of RAM, no PCI or other hard disk, etc.,
and just want to run Linux, the best option is to use the ``virt`` board. This
is a platform which doesn't correspond to any real hardware and is designed for
use in virtual machines. You'll need to compile Linux with a suitable
configuration for running on the ``virt`` board. ``virt`` supports PCI, virtio
and large amounts of RAM.

Board-specific documentation
============================

..
   This table of contents should be kept sorted alphabetically
   by the title text of each file, which isn't the same ordering
   as an alphabetical sort by filename.

.. toctree::
   :maxdepth: 1

   openrisc/or1k-sim
   openrisc/virt

Emulated CPU architecture support
=================================

.. toctree::
   openrisc/emulation

OpenRISC CPU features
=====================

.. toctree::
   openrisc/cpu-features
