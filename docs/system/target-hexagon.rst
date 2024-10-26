.. _Hexagon-System-emulator:

Hexagon System emulator
-----------------------

Use the ``qemu-system-hexagon`` executable to simulate a 32-bit Hexagon
machine.

Hexagon Machines
================

Hexagon DSPs are suited to various functions and generally appear in a
"DSP subsystem" of a larger system-on-chip (SoC).

Hexagon DSPs are often included in a subsystem that looks like the diagram
below.  Instructions are loaded into DDR before the DSP is brought out of
reset and the first instructions are fetched from DDR via the EVB/reset vector.

In a real system, a TBU/SMMU would normally arbitrate AXI accesses but
we don't have a need to model that for QEMU.

Hexagon DSP cores use simultaneous multithreading (SMT) with as many as 8
hardware threads.

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

* L2VIC: the L2 vectored interrupt controller.  Supports 1024 input
  interrupts, edge- or level-triggered.  The core ISA has system registers
  ``VID``, ``VID1`` which read through to the L2VIC device.
* QTimer: ARMSSE-compatible programmable timer device. Its interrupts are
  wired to the L2VIC.  System registers ``TIMER``, ``UTIMER`` read
  through to the QTimer device.
* QDSP6SS: DSP subsystem features, accessible to the entire SoC, including
  DSP NMI, watchdog, reset, etc.  Not yet modeled in QEMU.
* CSR: Configuration/Status Registers.  Not yet modeled in QEMU.
* TCM: DSP-exclusive tightly-coupled memory.  This memory can be used for
  DSPs when isolated from DDR and in some bootstrapping modes.
* VTCM: DSP-exclusive vector tightly-coupled memory.  This memory is accessed
  by some HVX instructions.
* HVX: the vector coprocessor supports 64 and 128-byte vector registers.
  64-byte mode is not implemented in QEMU.


Bootstrapping
-------------
Hexagon systems do not generally have access to a block device.  So, for
QEMU the typical use case involves loading a binary or ELF file into memory
and executing from the indicated start address::

    $ qemu-system-hexagon -kernel ./prog -append 'arg1 arg2'

Semihosting
-----------
Hexagon supports a semihosting interface similar to other architectures'.
The ``trap0`` instruction can activate these semihosting calls so that the
guest software can access the host console and filesystem.  Semihosting
is not yet implemented in QEMU hexagon.

Hexagon Virtual Machine
-----------------------

The hexagon virtual machine is a hypervisor that can partition a single
Hexagon DSP among multiple guest operating systems, and abstracts the
specific details of a DSP architectural revision for the sake of consistency
among generations.

[minivm](https://github.com/quic/hexagonMVM) is a reference implementation
of this VM interface.


Hexagon Features
================
.. toctree::
   hexagon/emulation
   hexagon/cdsp

