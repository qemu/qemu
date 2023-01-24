Emulation
=========

QEMU's Tiny Code Generator (TCG) provides the ability to emulate a
number of CPU architectures on any supported host platform. Both
:ref:`System Emulation` and :ref:`User Mode Emulation` are supported
depending on the guest architecture.

.. list-table:: Supported Guest Architectures for Emulation
  :widths: 30 10 10 50
  :header-rows: 1

  * - Architecture (qemu name)
    - System
    - User
    - Notes
  * - Alpha
    - Yes
    - Yes
    - Legacy 64 bit RISC ISA developed by DEC
  * - Arm (arm, aarch64)
    - :ref:`Yes<ARM-System-emulator>`
    - Yes
    - Wide range of features, see :ref:`Arm Emulation` for details
  * - AVR
    - :ref:`Yes<AVR-System-emulator>`
    - No
    - 8 bit micro controller, often used in maker projects
  * - Cris
    - Yes
    - Yes
    - Embedded RISC chip developed by AXIS
  * - Hexagon
    - No
    - Yes
    - Family of DSPs by Qualcomm
  * - PA-RISC (hppa)
    - Yes
    - Yes
    - A legacy RISC system used in HP's old minicomputers
  * - x86 (i386, x86_64)
    - :ref:`Yes<QEMU-PC-System-emulator>`
    - Yes
    - The ubiquitous desktop PC CPU architecture, 32 and 64 bit.
  * - Loongarch
    - Yes
    - Yes
    - A MIPS-like 64bit RISC architecture developed in China
  * - m68k
    - :ref:`Yes<ColdFire-System-emulator>`
    - Yes
    - Motorola 68000 variants and ColdFire
  * - Microblaze
    - Yes
    - Yes
    - RISC based soft-core by Xilinx
  * - MIPS (mips*)
    - :ref:`Yes<MIPS-System-emulator>`
    - Yes
    - Venerable RISC architecture originally out of Stanford University
  * - Nios2
    - Yes
    - Yes
    - 32 bit embedded soft-core by Altera
  * - OpenRISC
    - :ref:`Yes<OpenRISC-System-emulator>`
    - Yes
    - Open source RISC architecture developed by the OpenRISC community
  * - Power (ppc, ppc64)
    - :ref:`Yes<PowerPC-System-emulator>`
    - Yes
    - A general purpose RISC architecture now managed by IBM
  * - RISC-V
    - :ref:`Yes<RISC-V-System-emulator>`
    - Yes
    - An open standard RISC ISA maintained by RISC-V International
  * - RX
    - :ref:`Yes<RX-System-emulator>`
    - No
    - A 32 bit micro controller developed by Renesas
  * - s390x
    - :ref:`Yes<s390x-System-emulator>`
    - Yes
    - A 64 bit CPU found in IBM's System Z mainframes
  * - sh4
    - Yes
    - Yes
    - A 32 bit RISC embedded CPU developed by Hitachi
  * - SPARC (sparc, sparc64)
    - :ref:`Yes<Sparc32-System-emulator>`
    - Yes
    - A RISC ISA originally developed by Sun Microsystems
  * - Tricore
    - Yes
    - No
    - A 32 bit RISC/uController/DSP developed by Infineon
  * - Xtensa
    - :ref:`Yes<Xtensa-System-emulator>`
    - Yes
    - A configurable 32 bit soft core now owned by Cadence

A number of features are are only available when running under
emulation including :ref:`Record/Replay<replay>` and :ref:`TCG Plugins`.
