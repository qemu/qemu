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

A number of features are only available when running under
emulation including :ref:`Record/Replay<replay>` and :ref:`TCG Plugins`.

.. _Semihosting:

Semihosting
-----------

Semihosting is a feature defined by the owner of the architecture to
allow programs to interact with a debugging host system. On real
hardware this is usually provided by an In-circuit emulator (ICE)
hooked directly to the board. QEMU's implementation allows for
semihosting calls to be passed to the host system or via the
``gdbstub``.

Generally semihosting makes it easier to bring up low level code before a
more fully functional operating system has been enabled. On QEMU it
also allows for embedded micro-controller code which typically doesn't
have a full libc to be run as "bare-metal" code under QEMU's user-mode
emulation. It is also useful for writing test cases and indeed a
number of compiler suites as well as QEMU itself use semihosting calls
to exit test code while reporting the success state.

Semihosting is only available using TCG emulation. This is because the
instructions to trigger a semihosting call are typically reserved
causing most hypervisors to trap and fault on them.

.. warning::
   Semihosting inherently bypasses any isolation there may be between
   the guest and the host. As a result a program using semihosting can
   happily trash your host system. You should only ever run trusted
   code with semihosting enabled.

Redirection
~~~~~~~~~~~

Semihosting calls can be re-directed to a (potentially remote) gdb
during debugging via the :ref:`gdbstub<GDB usage>`. Output to the
semihosting console is configured as a ``chardev`` so can be
redirected to a file, pipe or socket like any other ``chardev``
device.

Supported Targets
~~~~~~~~~~~~~~~~~

Most targets offer similar semihosting implementations with some
minor changes to define the appropriate instruction to encode the
semihosting call and which registers hold the parameters. They tend to
presents a simple POSIX-like API which allows your program to read and
write files, access the console and some other basic interactions.

For full details of the ABI for a particular target, and the set of
calls it provides, you should consult the semihosting specification
for that architecture.

.. note::
   QEMU makes an implementation decision to implement all file
   access in ``O_BINARY`` mode. The user-visible effect of this is
   regardless of the text/binary mode the program sets QEMU will
   always select a binary mode ensuring no line-terminator conversion
   is performed on input or output. This is because gdb semihosting
   support doesn't make the distinction between the modes and
   magically processing line endings can be confusing.

.. list-table:: Guest Architectures supporting Semihosting
  :widths: 10 10 80
  :header-rows: 1

  * - Architecture
    - Modes
    - Specification
  * - Arm
    - System and User-mode
    - https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
  * - m68k
    - System
    - https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=libgloss/m68k/m68k-semi.txt;hb=HEAD
  * - MIPS
    - System
    - Unified Hosting Interface (MD01069)
  * - Nios II
    - System
    - https://sourceware.org/git/gitweb.cgi?p=newlib-cygwin.git;a=blob;f=libgloss/nios2/nios2-semi.txt;hb=HEAD
  * - RISC-V
    - System and User-mode
    - https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
  * - Xtensa
    - System
    - Tensilica ISS SIMCALL
