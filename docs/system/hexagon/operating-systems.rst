.. SPDX-License-Identifier: GPL-2.0-or-later

Booting guest operating systems
===============================

Hexagon VM guests require a Hexagon VM host. QEMU ships board-specific
host firmware in ``pc-bios``. Guest operating systems boot with the
``hexagon_loadlinux_*`` images; the ``hexagon_booter_*`` images run H2
user programs instead (see :doc:`cdsp`). Select a matching machine,
firmware, and guest memory layout from :doc:`cdsp`.

Linux
-----

The kernel must be built for the Hexagon VM interface and include the
PL011 console and required virtio-mmio device drivers.

For a preloaded kernel and separate initramfs:

.. code-block:: sh

    qemu-system-hexagon -M qcs6490-cdsp -display none -serial stdio \
        -bios hexagon_loadlinux_qcs6490_cdsp \
        -kernel ./vmlinux -initrd ./initramfs.cpio.gz \
        -append "console=ttyAMA0 rdinit=/init"

``ttyAMA0`` selects the PL011 console. ``-append`` sets the boot arguments
in QEMU's DTB; the guest and its VM host determine how those arguments
are consumed. Add any platform-specific memory or delay-calibration
parameters required by the kernel image.

Guest operating systems on virt
--------------------------------

The ``virt`` machine boots a guest operating system through the same
firmware:

.. code-block:: sh

    qemu-system-hexagon -M virt -display none -serial stdio \
        -bios hexagon_loadlinux_sa8775p_cdsp \
        -kernel ./guest.elf -append "console=ttyAMA0"

QEMU loads the guest ELF at its declared physical addresses. QEMU enters
the firmware with the DTB address in ``r1:r0`` (see
:ref:`hexagon-vm-boot`). The guest must use the Hexagon VM interface for
interrupts and timers, the PL011 at ``0x10000000``, and the virtio-mmio
transports listed in :doc:`machines`.
