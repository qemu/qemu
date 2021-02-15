QEMU User space emulator
========================

Supported Operating Systems
---------------------------

The following OS are supported in user space emulation:

-  Linux (referred as qemu-linux-user)

-  BSD (referred as qemu-bsd-user)

Features
--------

QEMU user space emulation has the following notable features:

**System call translation:**
   QEMU includes a generic system call translator. This means that the
   parameters of the system calls can be converted to fix endianness and
   32/64-bit mismatches between hosts and targets. IOCTLs can be
   converted too.

**POSIX signal handling:**
   QEMU can redirect to the running program all signals coming from the
   host (such as ``SIGALRM``), as well as synthesize signals from
   virtual CPU exceptions (for example ``SIGFPE`` when the program
   executes a division by zero).

   QEMU relies on the host kernel to emulate most signal system calls,
   for example to emulate the signal mask. On Linux, QEMU supports both
   normal and real-time signals.

**Threading:**
   On Linux, QEMU can emulate the ``clone`` syscall and create a real
   host thread (with a separate virtual CPU) for each emulated thread.
   Note that not all targets currently emulate atomic operations
   correctly. x86 and Arm use a global lock in order to preserve their
   semantics.

QEMU was conceived so that ultimately it can emulate itself. Although it
is not very useful, it is an important test to show the power of the
emulator.

Linux User space emulator
-------------------------

Command line options
~~~~~~~~~~~~~~~~~~~~

::

   qemu-i386 [-h] [-d] [-L path] [-s size] [-cpu model] [-g port] [-B offset] [-R size] program [arguments...]

``-h``
   Print the help

``-L path``
   Set the x86 elf interpreter prefix (default=/usr/local/qemu-i386)

``-s size``
   Set the x86 stack size in bytes (default=524288)

``-cpu model``
   Select CPU model (-cpu help for list and additional feature
   selection)

``-E var=value``
   Set environment var to value.

``-U var``
   Remove var from the environment.

``-B offset``
   Offset guest address by the specified number of bytes. This is useful
   when the address region required by guest applications is reserved on
   the host. This option is currently only supported on some hosts.

``-R size``
   Pre-allocate a guest virtual address space of the given size (in
   bytes). \"G\", \"M\", and \"k\" suffixes may be used when specifying
   the size.

Debug options:

``-d item1,...``
   Activate logging of the specified items (use '-d help' for a list of
   log items)

``-p pagesize``
   Act as if the host page size was 'pagesize' bytes

``-g port``
   Wait gdb connection to port

``-singlestep``
   Run the emulation in single step mode.

Environment variables:

QEMU_STRACE
   Print system calls and arguments similar to the 'strace' program
   (NOTE: the actual 'strace' program will not work because the user
   space emulator hasn't implemented ptrace). At the moment this is
   incomplete. All system calls that don't have a specific argument
   format are printed with information for six arguments. Many
   flag-style arguments don't have decoders and will show up as numbers.

Other binaries
~~~~~~~~~~~~~~

-  user mode (Alpha)

   * ``qemu-alpha`` TODO.

-  user mode (Arm)

   * ``qemu-armeb`` TODO.

   * ``qemu-arm`` is also capable of running Arm \"Angel\" semihosted ELF
     binaries (as implemented by the arm-elf and arm-eabi Newlib/GDB
     configurations), and arm-uclinux bFLT format binaries.

-  user mode (ColdFire)

-  user mode (M68K)

   * ``qemu-m68k`` is capable of running semihosted binaries using the BDM
     (m5xxx-ram-hosted.ld) or m68k-sim (sim.ld) syscall interfaces, and
     coldfire uClinux bFLT format binaries.

   The binary format is detected automatically.

-  user mode (Cris)

   * ``qemu-cris`` TODO.

-  user mode (i386)

   * ``qemu-i386`` TODO.
   * ``qemu-x86_64`` TODO.

-  user mode (Microblaze)

   * ``qemu-microblaze`` TODO.

-  user mode (MIPS)

   * ``qemu-mips`` executes 32-bit big endian MIPS binaries (MIPS O32 ABI).

   * ``qemu-mipsel`` executes 32-bit little endian MIPS binaries (MIPS O32 ABI).

   * ``qemu-mips64`` executes 64-bit big endian MIPS binaries (MIPS N64 ABI).

   * ``qemu-mips64el`` executes 64-bit little endian MIPS binaries (MIPS N64
     ABI).

   * ``qemu-mipsn32`` executes 32-bit big endian MIPS binaries (MIPS N32 ABI).

   * ``qemu-mipsn32el`` executes 32-bit little endian MIPS binaries (MIPS N32
     ABI).

-  user mode (NiosII)

   * ``qemu-nios2`` TODO.

-  user mode (PowerPC)

   * ``qemu-ppc64abi32`` TODO.
   * ``qemu-ppc64`` TODO.
   * ``qemu-ppc`` TODO.

-  user mode (SH4)

   * ``qemu-sh4eb`` TODO.
   * ``qemu-sh4`` TODO.

-  user mode (SPARC)

   * ``qemu-sparc`` can execute Sparc32 binaries (Sparc32 CPU, 32 bit ABI).

   * ``qemu-sparc32plus`` can execute Sparc32 and SPARC32PLUS binaries
     (Sparc64 CPU, 32 bit ABI).

   * ``qemu-sparc64`` can execute some Sparc64 (Sparc64 CPU, 64 bit ABI) and
     SPARC32PLUS binaries (Sparc64 CPU, 32 bit ABI).

BSD User space emulator
-----------------------

BSD Status
~~~~~~~~~~

-  target Sparc64 on Sparc64: Some trivial programs work.

Quick Start
~~~~~~~~~~~

In order to launch a BSD process, QEMU needs the process executable
itself and all the target dynamic libraries used by it.

-  On Sparc64, you can just try to launch any process by using the
   native libraries::

      qemu-sparc64 /bin/ls

Command line options
~~~~~~~~~~~~~~~~~~~~

::

   qemu-sparc64 [-h] [-d] [-L path] [-s size] [-bsd type] program [arguments...]

``-h``
   Print the help

``-L path``
   Set the library root path (default=/)

``-s size``
   Set the stack size in bytes (default=524288)

``-ignore-environment``
   Start with an empty environment. Without this option, the initial
   environment is a copy of the caller's environment.

``-E var=value``
   Set environment var to value.

``-U var``
   Remove var from the environment.

``-bsd type``
   Set the type of the emulated BSD Operating system. Valid values are
   FreeBSD, NetBSD and OpenBSD (default).

Debug options:

``-d item1,...``
   Activate logging of the specified items (use '-d help' for a list of
   log items)

``-p pagesize``
   Act as if the host page size was 'pagesize' bytes

``-singlestep``
   Run the emulation in single step mode.
