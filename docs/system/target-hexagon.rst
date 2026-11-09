.. SPDX-License-Identifier: GPL-2.0-or-later

.. _Hexagon-System-emulator:

Hexagon System emulator
-----------------------

Use the ``qemu-system-hexagon`` executable to simulate a 32-bit Hexagon
machine.

DSP subsystem
=============

Hexagon DSPs are suited to various functions and generally appear in a
"DSP subsystem" of a larger system-on-chip (SoC).

Hexagon DSPs are often included in a subsystem that looks like the diagram
below.  Instructions are loaded into DDR before the DSP is brought out of
reset and the first instructions are fetched from DDR via the reset vector.
QEMU's ELF boot path instead starts at the ELF entry point; the guest sets
up its own ``EVB``.

In a real system, a TBU/SMMU would normally arbitrate AXI accesses but
we don't have a need to model that for QEMU.

Hexagon DSP cores use simultaneous multithreading (SMT). The number of
hardware threads depends on the machine.

.. admonition:: Diagram

 .. code:: text

              AHB (local) bus                     AXI (global) bus
                    │                                 │
                    │                                 │
       ┌─────────┐  │       ┌─────────────────┐       │
       │ L2VIC   ├──┤       │                 │       │
       │         ├──┼───────►                 ├───────┤
       └─────▲───┘  │       │   Hexagon DSP   │       │
             │      │       │                 │       │        ┌─────┐
             │      │       │    N threads    │       │        │ DDR │
             │      ├───────┤                 │       │        │     │
        ┌────┴──┐   │       │                 │       ├────────┤     │
        │QTimer ├───┤       │                 │       │        │     │
        │       │   │       │                 │       │        │     │
        └───────┘   │       │   ┌─────────┐   │       │        │     │
                    │       │  ┌─────────┐│   │       │        │     │
        ┌───────┐   │       │  │  HVX xM ││   │       │        │     │
        │QDSP6SS├───┤       │  │         │┘   │       │        │     │
        └───────┘   │       │  └─────────┘    │       │        └─────┘
                    │       │                 │       │
        ┌───────┐   │       └─────────────────┘       │
        │  CSR  ├───┤
        └───────┘   │   ┌──────┐   ┌───────────┐
                    │   │ TCM  │   │   VTCM    │
                        │      │   │           │
                        └──────┘   │           │
                                   │           │
                                   │           │
                                   │           │
                                   └───────────┘

Components
----------
The devices instantiated depend on the machine. QEMU implements the
interrupt controller, timer, configuration table, and accelerator contexts;
local RAM regions are provided where the machine configuration calls for
them.

* L2VIC: the L2 vectored interrupt controller.  Supports 1024 input
  interrupts, edge- or level-triggered.  The core ISA has system registers
  ``VID``, ``VID1`` which read through to the L2VIC device.
* QTimer: programmable timer device. Its interrupts are
  wired to the L2VIC.  System registers ``TIMER``, ``UTIMER`` read
  through to the QTimer device.
* QDSP6SS: DSP subsystem features, accessible to the entire SoC, including
  DSP NMI, watchdog, reset, etc. Board models provide selected registers
  for firmware initialization.
* CSR: the QTimer control registers are mapped at the machine's CSR base
  address; see :doc:`hexagon/machines`.
* TCM: DSP-exclusive tightly-coupled memory.  This memory can be used for
  DSPs when isolated from DDR and in some bootstrapping modes.
* VTCM: DSP-exclusive vector tightly-coupled memory.  This memory is accessed
  by some HVX instructions.
* HVX: the vector coprocessor. See :doc:`hexagon/emulation` for supported
  modes and instruction sets.


Bootstrapping
-------------
Use ``-kernel`` to load an ELF file into memory and start execution at
its entry point::

    $ qemu-system-hexagon -M V68N_1024 -kernel ./prog.elf -append 'arg1 arg2'

Semihosting
-----------
Hexagon supports a semihosting interface similar to other architectures'.
The ``trap0`` instruction can activate these semihosting calls so that the
guest software can access the host console and filesystem. Read the
`Hexagon Semihosting Specification
<https://docs.qualcomm.com/doc/80-N2040-101/>`__
for details. Semihosting is enabled by default on hexagon-sim-compatible
machines. This can be further configured through ``-semihosting-config``.

.. toctree::
   :caption: Hexagon documentation

   hexagon/machines
   hexagon/boot
   hexagon/emulation
   hexagon/cdsp
