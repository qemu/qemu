..
   Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
   Copyright (c) 2019, Linaro Limited
   Written by Emilio Cota and Alex Bennée

QEMU TCG Plugins
================

QEMU TCG plugins provide a way for users to run experiments taking
advantage of the total system control emulation can have over a guest.
It provides a mechanism for plugins to subscribe to events during
translation and execution and optionally callback into the plugin
during these events. TCG plugins are unable to change the system state
only monitor it passively. However they can do this down to an
individual instruction granularity including potentially subscribing
to all load and store operations.

Usage
-----

Any QEMU binary with TCG support has plugins enabled by default.
Earlier releases needed to be explicitly enabled with::

  configure --enable-plugins

Once built a program can be run with multiple plugins loaded each with
their own arguments::

  $QEMU $OTHER_QEMU_ARGS \
      -plugin contrib/plugin/libhowvec.so,inline=on,count=hint \
      -plugin contrib/plugin/libhotblocks.so

Arguments are plugin specific and can be used to modify their
behaviour. In this case the howvec plugin is being asked to use inline
ops to count and break down the hint instructions by type.

Linux user-mode emulation also evaluates the environment variable
``QEMU_PLUGIN``::

  QEMU_PLUGIN="file=contrib/plugins/libhowvec.so,inline=on,count=hint" $QEMU

Writing plugins
---------------

API versioning
~~~~~~~~~~~~~~

This is a new feature for QEMU and it does allow people to develop
out-of-tree plugins that can be dynamically linked into a running QEMU
process. However the project reserves the right to change or break the
API should it need to do so. The best way to avoid this is to submit
your plugin upstream so they can be updated if/when the API changes.

All plugins need to declare a symbol which exports the plugin API
version they were built against. This can be done simply by::

  QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

The core code will refuse to load a plugin that doesn't export a
``qemu_plugin_version`` symbol or if plugin version is outside of QEMU's
supported range of API versions.

Additionally the ``qemu_info_t`` structure which is passed to the
``qemu_plugin_install`` method of a plugin will detail the minimum and
current API versions supported by QEMU. The API version will be
incremented if new APIs are added. The minimum API version will be
incremented if existing APIs are changed or removed.

Lifetime of the query handle
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each callback provides an opaque anonymous information handle which
can usually be further queried to find out information about a
translation, instruction or operation. The handles themselves are only
valid during the lifetime of the callback so it is important that any
information that is needed is extracted during the callback and saved
by the plugin.

Plugin life cycle
~~~~~~~~~~~~~~~~~

First the plugin is loaded and the public qemu_plugin_install function
is called. The plugin will then register callbacks for various plugin
events. Generally plugins will register a handler for the *atexit*
if they want to dump a summary of collected information once the
program/system has finished running.

When a registered event occurs the plugin callback is invoked. The
callbacks may provide additional information. In the case of a
translation event the plugin has an option to enumerate the
instructions in a block of instructions and optionally register
callbacks to some or all instructions when they are executed.

There is also a facility to add an inline event where code to
increment a counter can be directly inlined with the translation.
Currently only a simple increment is supported. This is not atomic so
can miss counts. If you want absolute precision you should use a
callback which can then ensure atomicity itself.

Finally when QEMU exits all the registered *atexit* callbacks are
invoked.

Exposure of QEMU internals
~~~~~~~~~~~~~~~~~~~~~~~~~~

The plugin architecture actively avoids leaking implementation details
about how QEMU's translation works to the plugins. While there are
conceptions such as translation time and translation blocks the
details are opaque to plugins. The plugin is able to query select
details of instructions and system configuration only through the
exported *qemu_plugin* functions.

API
~~~

.. kernel-doc:: include/qemu/qemu-plugin.h

Internals
---------

Locking
~~~~~~~

We have to ensure we cannot deadlock, particularly under MTTCG. For
this we acquire a lock when called from plugin code. We also keep the
list of callbacks under RCU so that we do not have to hold the lock
when calling the callbacks. This is also for performance, since some
callbacks (e.g. memory access callbacks) might be called very
frequently.

  * A consequence of this is that we keep our own list of CPUs, so that
    we do not have to worry about locking order wrt cpu_list_lock.
  * Use a recursive lock, since we can get registration calls from
    callbacks.

As a result registering/unregistering callbacks is "slow", since it
takes a lock. But this is very infrequent; we want performance when
calling (or not calling) callbacks, not when registering them. Using
RCU is great for this.

We support the uninstallation of a plugin at any time (e.g. from
plugin callbacks). This allows plugins to remove themselves if they no
longer want to instrument the code. This operation is asynchronous
which means callbacks may still occur after the uninstall operation is
requested. The plugin isn't completely uninstalled until the safe work
has executed while all vCPUs are quiescent.

Example Plugins
---------------

There are a number of plugins included with QEMU and you are
encouraged to contribute your own plugins plugins upstream. There is a
``contrib/plugins`` directory where they can go.

- tests/plugins

These are some basic plugins that are used to test and exercise the
API during the ``make check-tcg`` target.

- contrib/plugins/hotblocks.c

The hotblocks plugin allows you to examine the where hot paths of
execution are in your program. Once the program has finished you will
get a sorted list of blocks reporting the starting PC, translation
count, number of instructions and execution count. This will work best
with linux-user execution as system emulation tends to generate
re-translations as blocks from different programs get swapped in and
out of system memory.

If your program is single-threaded you can use the ``inline`` option for
slightly faster (but not thread safe) counters.

Example::

  ./aarch64-linux-user/qemu-aarch64 \
    -plugin contrib/plugins/libhotblocks.so -d plugin \
    ./tests/tcg/aarch64-linux-user/sha1
  SHA1=15dd99a1991e0b3826fede3deffc1feba42278e6
  collected 903 entries in the hash table
  pc, tcount, icount, ecount
  0x0000000041ed10, 1, 5, 66087
  0x000000004002b0, 1, 4, 66087
  ...

- contrib/plugins/hotpages.c

Similar to hotblocks but this time tracks memory accesses::

  ./aarch64-linux-user/qemu-aarch64 \
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

  * sortby=reads|writes|address

  Log the data sorted by either the number of reads, the number of writes, or
  memory address. (Default: entries are sorted by the sum of reads and writes)

  * io=on

  Track IO addresses. Only relevant to full system emulation. (Default: off)

  * pagesize=N

  The page size used. (Default: N = 4096)

- contrib/plugins/howvec.c

This is an instruction classifier so can be used to count different
types of instructions. It has a number of options to refine which get
counted. You can give a value to the ``count`` argument for a class of
instructions to break it down fully, so for example to see all the system
registers accesses::

  ./aarch64-softmmu/qemu-system-aarch64 $(QEMU_ARGS) \
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

- contrib/plugins/lockstep.c

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


  ./sparc-softmmu/qemu-system-sparc -monitor none -parallel none \
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

- contrib/plugins/hwprofile.c

The hwprofile tool can only be used with system emulation and allows
the user to see what hardware is accessed how often. It has a number of options:

 * track=read or track=write

 By default the plugin tracks both reads and writes. You can use one
 of these options to limit the tracking to just one class of accesses.

 * source

 Will include a detailed break down of what the guest PC that made the
 access was. Not compatible with the pattern option. Example output::

   cirrus-low-memory @ 0xfffffd00000a0000
    pc:fffffc0000005cdc, 1, 256
    pc:fffffc0000005ce8, 1, 256
    pc:fffffc0000005cec, 1, 256

 * pattern

 Instead break down the accesses based on the offset into the HW
 region. This can be useful for seeing the most used registers of a
 device. Example output::

    pci0-conf @ 0xfffffd01fe000000
      off:00000004, 1, 1
      off:00000010, 1, 3
      off:00000014, 1, 3
      off:00000018, 1, 2
      off:0000001c, 1, 2
      off:00000020, 1, 2
      ...

- contrib/plugins/execlog.c

The execlog tool traces executed instructions with memory access. It can be used
for debugging and security analysis purposes.
Please be aware that this will generate a lot of output.

The plugin takes no argument::

  qemu-system-arm $(QEMU_ARGS) \
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

- contrib/plugins/cache.c

Cache modelling plugin that measures the performance of a given L1 cache
configuration, and optionally a unified L2 per-core cache when a given working
set is run::

    qemu-x86_64 -plugin ./contrib/plugins/libcache.so \
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

  * limit=N

  Print top N icache and dcache thrashing instructions along with their
  address, number of misses, and its disassembly. (default: 32)

  * icachesize=N
  * iblksize=B
  * iassoc=A

  Instruction cache configuration arguments. They specify the cache size, block
  size, and associativity of the instruction cache, respectively.
  (default: N = 16384, B = 64, A = 8)

  * dcachesize=N
  * dblksize=B
  * dassoc=A

  Data cache configuration arguments. They specify the cache size, block size,
  and associativity of the data cache, respectively.
  (default: N = 16384, B = 64, A = 8)

  * evict=POLICY

  Sets the eviction policy to POLICY. Available policies are: :code:`lru`,
  :code:`fifo`, and :code:`rand`. The plugin will use the specified policy for
  both instruction and data caches. (default: POLICY = :code:`lru`)

  * cores=N

  Sets the number of cores for which we maintain separate icache and dcache.
  (default: for linux-user, N = 1, for full system emulation: N = cores
  available to guest)

  * l2=on

  Simulates a unified L2 cache (stores blocks for both instructions and data)
  using the default L2 configuration (cache size = 2MB, associativity = 16-way,
  block size = 64B).

  * l2cachesize=N
  * l2blksize=B
  * l2assoc=A

  L2 cache configuration arguments. They specify the cache size, block size, and
  associativity of the L2 cache, respectively. Setting any of the L2
  configuration arguments implies ``l2=on``.
  (default: N = 2097152 (2MB), B = 64, A = 16)
