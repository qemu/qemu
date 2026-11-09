.. SPDX-License-Identifier: GPL-2.0-or-later

Hexagon machines
================

Choose a machine that matches the runtime environment for the program and
the Hexagon ISA it targets. Hexagon machines fall into two broad categories:
simulator-compatible machines and board-specific DSP subsystem machines.

Simulator-compatible machines
-----------------------------

Simulator-compatible machines support standalone SDK OS and QuRT programs
intended for the Hexagon simulator environment. They provide the common
Hexagon system facilities expected by those programs, including interrupt
and timer services, local memory, semihosting, and a configuration table
that describes the selected model's capabilities.

Select the model that matches the program's target ISA and threading
requirements. ``V66G_1024`` is the default when ``-M`` is omitted; ``sim``
selects the most modern simulator-style machine.

The virtual machine (``virt``)
------------------------------

``virt`` is a synthetic simulator-compatible board for operating systems and
bare-metal programs. QEMU supplies a device tree that describes the virtual
hardware available to the guest, including CPUs, interrupt controller,
console, virtio devices, and accelerator capabilities. See :doc:`boot` for
its boot protocol.

Board-specific CDSP and NSP machines
------------------------------------

Board-specific CDSP and NSP machines model the DSP subsystem of a named
SoC.

Firmware choices are listed in :doc:`cdsp`.
