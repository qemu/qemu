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
  * - LoongArch
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
   happily trash your host system. Some semihosting calls (e.g.
   ``SYS_READC``) can block execution indefinitely. You should only
   ever run trusted code with semihosting enabled.

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
  * - RISC-V
    - System and User-mode
    - https://github.com/riscv-non-isa/riscv-semihosting/blob/main/riscv-semihosting.adoc
  * - Xtensa
    - System
    - Tensilica ISS SIMCALL

.. _tcg-plugins:

TCG Plugins
-----------

QEMU TCG plugins provide a way for users to run experiments taking
advantage of the total system control emulation can have over a guest.
It provides a mechanism for plugins to subscribe to events during
translation and execution and optionally callback into the plugin
during these events. TCG plugins are unable to change the system state
only monitor it passively. However they can do this down to an
individual instruction granularity including potentially subscribing
to all load and store operations.

See the developer section of the manual for details about
:ref:`writing plugins<TCG Plugins>`.

Usage
~~~~~

Any QEMU binary with TCG support has plugins enabled by default.
Earlier releases needed to be explicitly enabled with::

  configure --enable-plugins

Once built a program can be run with multiple plugins loaded each with
their own arguments::

  $QEMU $OTHER_QEMU_ARGS \
      -plugin contrib/plugins/libhowvec.so,inline=on,count=hint \
      -plugin contrib/plugins/libhotblocks.so

Arguments are plugin specific and can be used to modify their
behaviour. In this case the howvec plugin is being asked to use inline
ops to count and break down the hint instructions by type.

Linux user-mode emulation also evaluates the environment variable
``QEMU_PLUGIN``::

  QEMU_PLUGIN="file=contrib/plugins/libhowvec.so,inline=on,count=hint" $QEMU

QEMU plugins avoid to write directly to stdin/stderr, and use the log provided
by the API (see function ``qemu_plugin_outs``).
To show output, you may use this additional parameter::

  $QEMU $OTHER_QEMU_ARGS \
    -d plugin \
    -plugin contrib/plugins/libhowvec.so,inline=on,count=hint

Example Plugins
~~~~~~~~~~~~~~~

There are a number of plugins included with QEMU and you are
encouraged to contribute your own plugins plugins upstream. There is a
``contrib/plugins`` directory where they can go. There are also some
basic plugins that are used to test and exercise the API during the
``make check-tcg`` target in ``tests/tcg/plugins`` that are never the
less useful for basic analysis.

Empty
.....

``tests/tcg/plugins/empty.c``

Purely a test plugin for measuring the overhead of the plugins system
itself. Does no instrumentation.

Basic Blocks
............

``tests/tcg/plugins/bb.c``

A very basic plugin which will measure execution in coarse terms as
each basic block is executed. By default the results are shown once
execution finishes::

  $ qemu-aarch64 -plugin tests/plugin/libbb.so \
      -d plugin ./tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  bb's: 2277338, insns: 158483046

Behaviour can be tweaked with the following arguments:

.. list-table:: Basic Block plugin arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - inline=true|false
    - Use faster inline addition of a single counter.
  * - idle=true|false
    - Dump the current execution stats whenever the guest vCPU idles

Basic Block Vectors
...................

``contrib/plugins/bbv.c``

The bbv plugin allows you to generate basic block vectors for use with the
`SimPoint <https://cseweb.ucsd.edu/~calder/simpoint/>`__ analysis tool.

.. list-table:: Basic block vectors arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - interval=N
    - The interval to generate a basic block vector specified by the number of
      instructions (Default: N = 100000000)
  * - outfile=PATH
    - The path to output files.
      It will be suffixed with ``.N.bb`` where ``N`` is a vCPU index.

Example::

  $ qemu-aarch64 \
    -plugin contrib/plugins/libbbv.so,interval=100,outfile=sha1 \
    tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  $ du sha1.0.bb
  23128   sha1.0.bb

Instruction
...........

``tests/tcg/plugins/insn.c``

This is a basic instruction level instrumentation which can count the
number of instructions executed on each core/thread::

  $ qemu-aarch64 -plugin tests/plugin/libinsn.so \
      -d plugin ./tests/tcg/aarch64-linux-user/threadcount
  Created 10 threads
  Done
  cpu 0 insns: 46765
  cpu 1 insns: 3694
  cpu 2 insns: 3694
  cpu 3 insns: 2994
  cpu 4 insns: 1497
  cpu 5 insns: 1497
  cpu 6 insns: 1497
  cpu 7 insns: 1497
  total insns: 63135

Behaviour can be tweaked with the following arguments:

.. list-table:: Instruction plugin arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - inline=true|false
    - Use faster inline addition of a single counter.
  * - sizes=true|false
    - Give a summary of the instruction sizes for the execution
  * - match=<string>
    - Only instrument instructions matching the string prefix

The ``match`` option will show some basic stats including how many
instructions have executed since the last execution. For
example::

   $ qemu-aarch64 -plugin tests/plugin/libinsn.so,match=bl \
       -d plugin ./tests/tcg/aarch64-linux-user/sha512-vector
   ...
   0x40069c, 'bl #0x4002b0', 10 hits, 1093 match hits, Δ+1257 since last match, 98 avg insns/match
   0x4006ac, 'bl #0x403690', 10 hits, 1094 match hits, Δ+47 since last match, 98 avg insns/match
   0x4037fc, 'bl #0x4002b0', 18 hits, 1095 match hits, Δ+22 since last match, 98 avg insns/match
   0x400720, 'bl #0x403690', 10 hits, 1096 match hits, Δ+58 since last match, 98 avg insns/match
   0x4037fc, 'bl #0x4002b0', 19 hits, 1097 match hits, Δ+22 since last match, 98 avg insns/match
   0x400730, 'bl #0x403690', 10 hits, 1098 match hits, Δ+33 since last match, 98 avg insns/match
   0x4037ac, 'bl #0x4002b0', 12 hits, 1099 match hits, Δ+20 since last match, 98 avg insns/match
   ...

For more detailed execution tracing see the ``execlog`` plugin for
other options.

Memory
......

``tests/tcg/plugins/mem.c``

Basic instruction level memory instrumentation::

  $ qemu-aarch64 -plugin tests/plugin/libmem.so,inline=true \
      -d plugin ./tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  inline mem accesses: 79525013

Behaviour can be tweaked with the following arguments:

.. list-table:: Memory plugin arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - inline=true|false
    - Use faster inline addition of a single counter
  * - callback=true|false
    - Use callbacks on each memory instrumentation.
  * - hwaddr=true|false
    - Count IO accesses (only for system emulation)

System Calls
............

``tests/tcg/plugins/syscall.c``

A basic syscall tracing plugin. This only works for user-mode. By
default it will give a summary of syscall stats at the end of the
run::

  $ qemu-aarch64 -plugin tests/plugin/libsyscall \
      -d plugin ./tests/tcg/aarch64-linux-user/threadcount
  Created 10 threads
  Done
  syscall no.  calls  errors
  226          12     0
  99           11     11
  115          11     0
  222          11     0
  93           10     0
  220          10     0
  233          10     0
  215          8      0
  214          4      0
  134          2      0
  64           2      0
  96           1      0
  94           1      0
  80           1      0
  261          1      0
  78           1      0
  160          1      0
  135          1      0

Behaviour can be tweaked with the following arguments:

.. list-table:: Syscall plugin arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - print=true|false
    - Print the number of times each syscall is called
  * - log_writes=true|false
    - Log the buffer of each write syscall in hexdump format

Test inline operations
......................

``tests/plugins/inline.c``

This plugin is used for testing all inline operations, conditional callbacks and
scoreboard. It prints a per-cpu summary of all events.


Hot Blocks
..........

``contrib/plugins/hotblocks.c``

The hotblocks plugin allows you to examine the where hot paths of
execution are in your program. Once the program has finished you will
get a sorted list of blocks reporting the starting PC, translation
count, number of instructions and execution count. This will work best
with linux-user execution as system emulation tends to generate
re-translations as blocks from different programs get swapped in and
out of system memory.

Example::

  $ qemu-aarch64 \
    -plugin contrib/plugins/libhotblocks.so -d plugin \
    ./tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  collected 903 entries in the hash table
  pc, tcount, icount, ecount
  0x0000000041ed10, 1, 5, 66087
  0x000000004002b0, 1, 4, 66087
  ...


Hot Pages
.........

``contrib/plugins/hotpages.c``

Similar to hotblocks but this time tracks memory accesses::

  $ qemu-aarch64 \
    -plugin contrib/plugins/libhotpages.so -d plugin \
    ./tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  Addr, RCPUs, Reads, WCPUs, Writes
  0x000055007fe000, 0x0001, 31747952, 0x0001, 8835161
  0x000055007ff000, 0x0001, 29001054, 0x0001, 8780625
  0x00005500800000, 0x0001, 687465, 0x0001, 335857
  0x0000000048b000, 0x0001, 130594, 0x0001, 355
  0x0000000048a000, 0x0001, 1826, 0x0001, 11

The hotpages plugin can be configured using the following arguments:

.. list-table:: Hot pages arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - sortby=reads|writes|address
    - Log the data sorted by either the number of reads, the number of writes, or
      memory address. (Default: entries are sorted by the sum of reads and writes)
  * - io=on
    - Track IO addresses. Only relevant to full system emulation. (Default: off)
  * - pagesize=N
    - The page size used. (Default: N = 4096)

Instruction Distribution
........................

``contrib/plugins/howvec.c``

This is an instruction classifier so can be used to count different
types of instructions. It has a number of options to refine which get
counted. You can give a value to the ``count`` argument for a class of
instructions to break it down fully, so for example to see all the system
registers accesses::

  $ qemu-system-aarch64 $(QEMU_ARGS) \
    -append "root=/dev/sda2 systemd.unit=benchmark.service" \
    -smp 4 -plugin ./contrib/plugins/libhowvec.so,count=sreg -d plugin

which will lead to a sorted list after the class breakdown::

  Instruction Classes:
  Class:   UDEF                   not counted
  Class:   SVE                    (68 hits)
  Class:   PCrel addr             (47789483 hits)
  Class:   Add/Sub (imm)          (192817388 hits)
  Class:   Logical (imm)          (93852565 hits)
  Class:   Move Wide (imm)        (76398116 hits)
  Class:   Bitfield               (44706084 hits)
  Class:   Extract                (5499257 hits)
  Class:   Cond Branch (imm)      (147202932 hits)
  Class:   Exception Gen          (193581 hits)
  Class:     NOP                  not counted
  Class:   Hints                  (6652291 hits)
  Class:   Barriers               (8001661 hits)
  Class:   PSTATE                 (1801695 hits)
  Class:   System Insn            (6385349 hits)
  Class:   System Reg             counted individually
  Class:   Branch (reg)           (69497127 hits)
  Class:   Branch (imm)           (84393665 hits)
  Class:   Cmp & Branch           (110929659 hits)
  Class:   Tst & Branch           (44681442 hits)
  Class:   AdvSimd ldstmult       (736 hits)
  Class:   ldst excl              (9098783 hits)
  Class:   Load Reg (lit)         (87189424 hits)
  Class:   ldst noalloc pair      (3264433 hits)
  Class:   ldst pair              (412526434 hits)
  Class:   ldst reg (imm)         (314734576 hits)
  Class: Loads & Stores           (2117774 hits)
  Class: Data Proc Reg            (223519077 hits)
  Class: Scalar FP                (31657954 hits)
  Individual Instructions:
  Instr: mrs x0, sp_el0           (2682661 hits)  (op=0xd5384100/  System Reg)
  Instr: mrs x1, tpidr_el2        (1789339 hits)  (op=0xd53cd041/  System Reg)
  Instr: mrs x2, tpidr_el2        (1513494 hits)  (op=0xd53cd042/  System Reg)
  Instr: mrs x0, tpidr_el2        (1490823 hits)  (op=0xd53cd040/  System Reg)
  Instr: mrs x1, sp_el0           (933793 hits)   (op=0xd5384101/  System Reg)
  Instr: mrs x2, sp_el0           (699516 hits)   (op=0xd5384102/  System Reg)
  Instr: mrs x4, tpidr_el2        (528437 hits)   (op=0xd53cd044/  System Reg)
  Instr: mrs x30, ttbr1_el1       (480776 hits)   (op=0xd538203e/  System Reg)
  Instr: msr ttbr1_el1, x30       (480713 hits)   (op=0xd518203e/  System Reg)
  Instr: msr vbar_el1, x30        (480671 hits)   (op=0xd518c01e/  System Reg)
  ...

To find the argument shorthand for the class you need to examine the
source code of the plugin at the moment, specifically the ``*opt``
argument in the InsnClassExecCount tables.

Lockstep Execution
..................

``contrib/plugins/lockstep.c``

This is a debugging tool for developers who want to find out when and
where execution diverges after a subtle change to TCG code generation.
It is not an exact science and results are likely to be mixed once
asynchronous events are introduced. While the use of -icount can
introduce determinism to the execution flow it doesn't always follow
the translation sequence will be exactly the same. Typically this is
caused by a timer firing to service the GUI causing a block to end
early. However in some cases it has proved to be useful in pointing
people at roughly where execution diverges. The only argument you need
for the plugin is a path for the socket the two instances will
communicate over::


  $ qemu-system-sparc -monitor none -parallel none \
    -net none -M SS-20 -m 256 -kernel day11/zImage.elf \
    -plugin ./contrib/plugins/liblockstep.so,sockpath=lockstep-sparc.sock \
    -d plugin,nochain

which will eventually report::

  qemu-system-sparc: warning: nic lance.0 has no peer
  @ 0x000000ffd06678 vs 0x000000ffd001e0 (2/1 since last)
  @ 0x000000ffd07d9c vs 0x000000ffd06678 (3/1 since last)
  Δ insn_count @ 0x000000ffd07d9c (809900609) vs 0x000000ffd06678 (809900612)
    previously @ 0x000000ffd06678/10 (809900609 insns)
    previously @ 0x000000ffd001e0/4 (809900599 insns)
    previously @ 0x000000ffd080ac/2 (809900595 insns)
    previously @ 0x000000ffd08098/5 (809900593 insns)
    previously @ 0x000000ffd080c0/1 (809900588 insns)


Hardware Profile
................

``contrib/plugins/hwprofile.c``

The hwprofile tool can only be used with system emulation and allows
the user to see what hardware is accessed how often. It has a number of options:

.. list-table:: Hardware Profile arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - track=[read|write]
    - By default the plugin tracks both reads and writes. You can use
      this option to limit the tracking to just one class of accesses.
  * - source
    - Will include a detailed break down of what the guest PC that made the
      access was. Not compatible with the pattern option. Example output::

        cirrus-low-memory @ 0xfffffd00000a0000
         pc:fffffc0000005cdc, 1, 256
         pc:fffffc0000005ce8, 1, 256
         pc:fffffc0000005cec, 1, 256

  * - pattern
    - Instead break down the accesses based on the offset into the HW
      region. This can be useful for seeing the most used registers of
      a device. Example output::

        pci0-conf @ 0xfffffd01fe000000
          off:00000004, 1, 1
          off:00000010, 1, 3
          off:00000014, 1, 3
          off:00000018, 1, 2
          off:0000001c, 1, 2
          off:00000020, 1, 2
          ...


Execution Log
.............

``contrib/plugins/execlog.c``

The execlog tool traces executed instructions with memory access. It can be used
for debugging and security analysis purposes.
Please be aware that this will generate a lot of output.

The plugin needs default argument::

  $ qemu-system-arm $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libexeclog.so -d plugin

which will output an execution trace following this structure::

  # vCPU, vAddr, opcode, disassembly[, load/store, memory addr, device]...
  0, 0xa12, 0xf8012400, "movs r4, #0"
  0, 0xa14, 0xf87f42b4, "cmp r4, r6"
  0, 0xa16, 0xd206, "bhs #0xa26"
  0, 0xa18, 0xfff94803, "ldr r0, [pc, #0xc]", load, 0x00010a28, RAM
  0, 0xa1a, 0xf989f000, "bl #0xd30"
  0, 0xd30, 0xfff9b510, "push {r4, lr}", store, 0x20003ee0, RAM, store, 0x20003ee4, RAM
  0, 0xd32, 0xf9893014, "adds r0, #0x14"
  0, 0xd34, 0xf9c8f000, "bl #0x10c8"
  0, 0x10c8, 0xfff96c43, "ldr r3, [r0, #0x44]", load, 0x200000e4, RAM

Please note that you need to configure QEMU with Capstone support to get disassembly.

The output can be filtered to only track certain instructions or
addresses using the ``ifilter`` or ``afilter`` options. You can stack the
arguments if required::

  $ qemu-system-arm $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libexeclog.so,ifilter=st1w,afilter=0x40001808 -d plugin

This plugin can also dump registers when they change value. Specify the name of the
registers with multiple ``reg`` options. You can also use glob style matching if you wish::

  $ qemu-system-arm $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libexeclog.so,reg=\*_el2,reg=sp -d plugin

Be aware that each additional register to check will slow down
execution quite considerably. You can optimise the number of register
checks done by using the rdisas option. This will only instrument
instructions that mention the registers in question in disassembly.
This is not foolproof as some instructions implicitly change
instructions. You can use the ifilter to catch these cases::

  $ qemu-system-arm $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libexeclog.so,ifilter=msr,ifilter=blr,reg=x30,reg=\*_el1,rdisas=on

Cache Modelling
...............

``contrib/plugins/cache.c``

Cache modelling plugin that measures the performance of a given L1 cache
configuration, and optionally a unified L2 per-core cache when a given working
set is run::

  $ qemu-x86_64 -plugin ./contrib/plugins/libcache.so \
      -d plugin -D cache.log ./tests/tcg/x86_64-linux-user/float_convs

will report the following::

    core #, data accesses, data misses, dmiss rate, insn accesses, insn misses, imiss rate
    0       996695         508             0.0510%  2642799        18617           0.7044%

    address, data misses, instruction
    0x424f1e (_int_malloc), 109, movq %rax, 8(%rcx)
    0x41f395 (_IO_default_xsputn), 49, movb %dl, (%rdi, %rax)
    0x42584d (ptmalloc_init.part.0), 33, movaps %xmm0, (%rax)
    0x454d48 (__tunables_init), 20, cmpb $0, (%r8)
    ...

    address, fetch misses, instruction
    0x4160a0 (__vfprintf_internal), 744, movl $1, %ebx
    0x41f0a0 (_IO_setb), 744, endbr64
    0x415882 (__vfprintf_internal), 744, movq %r12, %rdi
    0x4268a0 (__malloc), 696, andq $0xfffffffffffffff0, %rax
    ...

The plugin has a number of arguments, all of them are optional:

.. list-table:: Cache modelling arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - limit=N
    - Print top N icache and dcache thrashing instructions along with
      their address, number of misses, and its disassembly. (default: 32)
  * - icachesize=N
      iblksize=B
      iassoc=A
    - Instruction cache configuration arguments. They specify the
      cache size, block size, and associativity of the instruction
      cache, respectively. (default: N = 16384, B = 64, A = 8)
  * - dcachesize=N
    - Data cache size (default: 16834)
  * - dblksize=B
    - Data cache block size (default: 64)
  * - dassoc=A
    - Data cache associativity (default: 8)
  * - evict=POLICY
    - Sets the eviction policy to POLICY. Available policies are:
      ``lru``, ``fifo``, and ``rand``. The plugin will use
      the specified policy for both instruction and data caches.
      (default: POLICY = ``lru``)
  * - cores=N
    - Sets the number of cores for which we maintain separate icache
      and dcache. (default: for linux-user, N = 1, for full system
      emulation: N = cores available to guest)
  * - l2=on
    - Simulates a unified L2 cache (stores blocks for both
      instructions and data) using the default L2 configuration (cache
      size = 2MB, associativity = 16-way, block size = 64B).
  * - l2cachesize=N
    - L2 cache size (default: 2097152 (2MB)), implies ``l2=on``
  * - l2blksize=B
    - L2 cache block size (default: 64), implies ``l2=on``
  * - l2assoc=A
    - L2 cache associativity (default: 16), implies ``l2=on``

Stop on Trigger
...............

``contrib/plugins/stoptrigger.c``

The stoptrigger plugin allows to setup triggers to stop emulation.
It can be used for research purposes to launch some code and precisely stop it
and understand where its execution flow went.

Two types of triggers can be configured: a count of instructions to stop at,
or an address to stop at. Multiple triggers can be set at once.

By default, QEMU will exit with return code 0. A custom return code can be
configured for each trigger using ``:CODE`` syntax.

For example, to stop at the 20-th instruction with return code 41, at address
0xd4 with return code 0 or at address 0xd8 with return code 42::

  $ qemu-system-aarch64 $(QEMU_ARGS) \
    -plugin ./contrib/plugins/libstoptrigger.so,icount=20:41,addr=0xd4,addr=0xd8:42 -d plugin

The plugin will log the reason of exit, for example::

  0xd4 reached, exiting

Limit instructions per second
.............................

This plugin can limit the number of Instructions Per Second that are executed::

    # get number of instructions
    $ num_insn=$(./build/qemu-x86_64 -plugin ./build/tests/plugin/libinsn.so -d plugin /bin/true |& grep total | sed -e 's/.*: //')
    # limit speed to execute in 10 seconds
    $ time ./build/qemu-x86_64 -plugin ./build/contrib/plugins/libips.so,ips=$(($num_insn/10)) /bin/true
    real 10.000s


.. list-table:: IPS arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - ips=N
    - Maximum number of instructions per cpu that can be executed in one second.
      The plugin will sleep when the given number of instructions is reached.
  * - ipq=N
    - Instructions per quantum. How many instructions before we re-calculate time.
      The lower the number the more accurate time will be, but the less efficient the plugin.
      Defaults to ips/10

Uftrace
.......

``contrib/plugins/uftrace.c``

This plugin generates a binary trace compatible with
`uftrace <https://github.com/namhyung/uftrace>`_.

Plugin supports aarch64 and x64, and works in user and system mode, allowing to
trace a system boot, which is not something possible usually.

In user mode, the memory mapping is directly copied from ``/proc/self/maps`` at
the end of execution. Uftrace should be able to retrieve symbols by itself,
without any additional step.
In system mode, the default memory mapping is empty, and you can generate
one (and associated symbols) using ``contrib/plugins/uftrace_symbols.py``.
Symbols must be present in ELF binaries.

It tracks the call stack (based on frame pointer analysis). Thus, your program
and its dependencies must be compiled using ``-fno-omit-frame-pointer
-mno-omit-leaf-frame-pointer``. In 2024, `Ubuntu and Fedora enabled it by
default again on x64
<https://www.brendangregg.com/blog/2024-03-17/the-return-of-the-frame-pointers.html>`_.
On aarch64, this is less of a problem, as they are usually part of the ABI,
except for leaf functions. That's true for user space applications, but not
necessarily for bare metal code. You can read this `section
<uftrace_build_system_example>` to easily build a system with frame pointers.

When tracing long scenarios (> 1 min), the generated trace can become very long,
making it hard to extract data from it. In this case, a simple solution is to
trace execution while generating a timestamped output log using
``qemu-system-aarch64 ... | ts "%s"``. Then, ``uftrace --time-range=start~end``
can be used to reduce trace for only this part of execution.

Performance wise, overhead compared to normal tcg execution is around x5-x15.

.. list-table:: Uftrace plugin arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - trace-privilege-level=[on|off]
    - Generate separate traces for each privilege level (Exception Level +
      Security State on aarch64, Rings on x64).

.. list-table:: uftrace_symbols.py arguments
  :widths: 20 80
  :header-rows: 1

  * - Option
    - Description
  * - elf_file [elf_file ...]
    - path to an ELF file. Use /path/to/file:0xdeadbeef to add a mapping offset.
  * - --prefix-symbols
    - prepend binary name to symbols

Example user trace
++++++++++++++++++

As an example, we can trace qemu itself running git::

    $ ./build/qemu-aarch64 -plugin \
      build/contrib/plugins/libuftrace.so \
      ./build/qemu-aarch64 /usr/bin/git --help

    # and generate a chrome trace directly
    $ uftrace dump --chrome | gzip > ~/qemu_aarch64_git_help.json.gz

For convenience, you can download this trace `qemu_aarch64_git_help.json.gz
<https://github.com/pbo-linaro/qemu-assets/raw/refs/heads/master/qemu-uftrace/qemu_aarch64_git_help.json.gz>`_.
Download it and open this trace on https://ui.perfetto.dev/. You can zoom in/out
using :kbd:`W`, :kbd:`A`, :kbd:`S`, :kbd:`D` keys.
Some sequences taken from this trace:

- Loading program and its interpreter

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/loader_exec.png?raw=true
   :height: 200px

- open syscall

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/open_syscall.png?raw=true
   :height: 200px

- TB creation

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/tb_translation.png?raw=true
   :height: 200px

It's usually better to use ``uftrace record`` directly. However, tracing
binaries through qemu-user can be convenient when you don't want to recompile
them (``uftrace record`` requires instrumentation), as long as symbols are
present.

Example system trace
++++++++++++++++++++

A full trace example (chrome trace, from instructions below) generated from a
system boot can be found `here
<https://github.com/pbo-linaro/qemu-assets/raw/refs/heads/master/qemu-uftrace/aarch64_boot.json.gz>`_.
Download it and open this trace on https://ui.perfetto.dev/. You can see code
executed for all privilege levels, and zoom in/out using
:kbd:`W`, :kbd:`A`, :kbd:`S`, :kbd:`D` keys. You can find below some sequences
taken from this trace:

- Two first stages of boot sequence in Arm Trusted Firmware (EL3 and S-EL1)

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/bl3_to_bl1.png?raw=true
   :height: 200px

- U-boot initialization (until code relocation, after which we can't track it)

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/uboot.png?raw=true
   :height: 200px

- Stat and open syscalls in kernel

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/stat.png?raw=true
   :height: 200px

- Timer interrupt

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/timer_interrupt.png?raw=true
   :height: 200px

- Poweroff sequence (from kernel back to firmware, NS-EL2 to EL3)

.. image:: https://github.com/pbo-linaro/qemu-assets/blob/master/qemu-uftrace/poweroff.png?raw=true
   :height: 200px

Build and run system example
++++++++++++++++++++++++++++

.. _uftrace_build_system_example:

Building a full system image with frame pointers is not trivial.

We provide a `simple way <https://github.com/pbo-linaro/qemu-linux-stack>`_ to
build an aarch64 system, combining Arm Trusted firmware, U-boot, Linux kernel
and debian userland. It's based on containers (``podman`` only) and
``qemu-user-static (binfmt)`` to make sure it's easily reproducible and does not depend
on machine where you build it.

You can follow the exact same instructions for a x64 system, combining edk2,
Linux, and Ubuntu, simply by switching to
`x86_64 <https://github.com/pbo-linaro/qemu-linux-stack/tree/x86_64>`_ branch.

To build the system::

    # Install dependencies
    $ sudo apt install -y podman qemu-user-static

    $ git clone https://github.com/pbo-linaro/qemu-linux-stack
    $ cd qemu-linux-stack
    $ ./build.sh

    # system can be started using:
    $ ./run.sh /path/to/qemu-system-aarch64

To generate a uftrace for a system boot from that::

    # run true and poweroff the system
    $ env INIT=true ./run.sh path/to/qemu-system-aarch64 \
      -plugin path/to/contrib/plugins/libuftrace.so,trace-privilege-level=on

    # generate symbols and memory mapping
    $ path/to/contrib/plugins/uftrace_symbols.py \
      --prefix-symbols \
      arm-trusted-firmware/build/qemu/debug/bl1/bl1.elf \
      arm-trusted-firmware/build/qemu/debug/bl2/bl2.elf \
      arm-trusted-firmware/build/qemu/debug/bl31/bl31.elf \
      u-boot/u-boot:0x60000000 \
      linux/vmlinux

    # inspect trace with
    $ uftrace replay

Uftrace allows to filter the trace, and dump flamegraphs, or a chrome trace.
This last one is very interesting to see visually the boot process::

    $ uftrace dump --chrome > boot.json
    # Open your browser, and load boot.json on https://ui.perfetto.dev/.

Long visual chrome traces can't be easily opened, thus, it might be
interesting to generate them around a particular point of execution::

    # execute qemu and timestamp output log
    $ env INIT=true ./run.sh path/to/qemu-system-aarch64 \
      -plugin path/to/contrib/plugins/libuftrace.so,trace-privilege-level=on |&
      ts "%s" | tee exec.log

    $ cat exec.log  | grep 'Run /init'
      1753122320 [   11.834391] Run /init as init process
      # init was launched at 1753122320

    # generate trace around init execution (2 seconds):
    $ uftrace dump --chrome --time-range=1753122320~1753122322 > init.json

Count traps
...........

``contrib/plugins/traps.c``

This plugin counts the number of interrupts (asyncronous events), exceptions
(synchronous events) and host calls (e.g. semihosting) per cpu.

Other emulation features
------------------------

When running system emulation you can also enable deterministic
execution which allows for repeatable record/replay debugging. See
:ref:`Record/Replay<replay>` for more details.
