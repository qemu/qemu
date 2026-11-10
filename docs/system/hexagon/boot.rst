.. SPDX-License-Identifier: GPL-2.0-or-later

Hexagon boot interface
======================

``qemu-system-hexagon`` loads the initial image. Guest startup code
initializes the stack, exception handling, and runtime state. When a
Hexagon VM host is loaded, it establishes the guest OS's boot environment.

Initial hardware state
----------------------

Only hardware thread zero starts executing. Other threads start powered
off.

Execution uses untranslated physical addresses in monitor mode. Guest software
must populate TLB entries before enabling the MMU, and set up exception vectors
and interrupt masks before enabling interrupts. See
:doc:`../../devel/hexagon-sys` for the system architecture.

QEMU sets thread zero's PC to the selected image entry address. ``EVB``
initially contains zero; ELF startup code installs its own aligned event
vector base.

Simulator-compatible machines
-----------------------------

``-kernel file`` loads a little-endian Hexagon ELF, places its loadable
segments according to their physical addresses, zero-fills the segments'
remaining memory, and starts thread zero at the ELF entry point. Link
startup code for untranslated execution, with the entry point accessible
at its linked address.

The virt machine
----------------

``-kernel file`` alone loads a Hexagon ELF into DDR, which starts at
``0x80000000``, and starts at its ELF entry point.

``-bios file`` alone loads a raw firmware image at the start of DDR and
starts executing there.

``-bios`` with ``-kernel`` boots the kernel through Hexagon VM host
firmware, as described in :ref:`hexagon-vm-boot`. Both ELF images load at
their declared physical addresses.

Otherwise, guest software reads the generated DTB at ``0x99800000``.
``-append`` sets its ``/chosen/bootargs`` property.

Board-specific machines
------------------------------------

Both ``-kernel`` and ``-bios`` accept Hexagon ELF images. The combinations
have different meanings:

.. list-table::
   :header-rows: 1

   * - Options
     - Loading and entry
   * - ``-kernel`` alone
     - Load segments at ELF physical addresses and start at the ELF entry.
   * - ``-bios`` alone
     - Load firmware segments at ELF physical addresses and start at its entry.
   * - ``-bios`` and ``-kernel``
     - Load firmware normally, relocate the kernel segments into the board's
       kernel window, and enter firmware through a QEMU boot stub.

The combined case is described in :ref:`hexagon-vm-boot`, using the kernel
base in :doc:`cdsp`.

``-dtb file`` replaces the generated tree. A nonempty ``-append`` value
sets its ``/chosen/bootargs``. A replacement tree must describe the
selected hardware-thread count, actual RAM, and matching device drivers.

.. _hexagon-vm-boot:

Booting a kernel through Hexagon VM host firmware
-------------------------------------------------

On the ``virt`` and board-specific machines, ``-bios`` with ``-kernel``
starts the kernel through Hexagon VM host firmware. QEMU loads the firmware
ELF as linked. On ``virt``, it also loads the kernel ELF as linked. The
board-specific machines add their configured kernel base to the kernel ELF's
physical load addresses, so those kernels are relative to the start of the
board's guest window. QEMU starts thread zero at a boot stub at
``0x99c00000``. The stub passes the DTB address with the low word in ``r0``
and the high word in ``r1``, then jumps to the firmware's ELF entry point.
The Hexagon VM host defines the child VM's register and execution state.

QEMU places the DTB after the loaded kernel.  QEMU copies it to guest RAM
before entering the firmware. An optional ``-initrd`` is placed
after the DTB base.  ``-initrd`` requires both ``-bios`` and ``-kernel``.

Console and semihosting
-----------------------

SDK simulator programs use semihosting for console I/O. The ``virt`` and
board-specific machines also provide a PL011 UART, whose host backend is
selected with ``-serial``.
