====================
Translator Internals
====================

QEMU is a dynamic translator. When it first encounters a piece of code,
it converts it to the host instruction set. Usually dynamic translators
are very complicated and highly CPU dependent. QEMU uses some tricks
which make it relatively easily portable and simple while achieving good
performances.

QEMU's dynamic translation backend is called TCG, for "Tiny Code
Generator". For more information, please take a look at ``tcg/README``.

Some notable features of QEMU's dynamic translator are:

CPU state optimisations
-----------------------

The target CPUs have many internal states which change the way it
evaluates instructions. In order to achieve a good speed, the
translation phase considers that some state information of the virtual
CPU cannot change in it. The state is recorded in the Translation
Block (TB). If the state changes (e.g. privilege level), a new TB will
be generated and the previous TB won't be used anymore until the state
matches the state recorded in the previous TB. The same idea can be applied
to other aspects of the CPU state.  For example, on x86, if the SS,
DS and ES segments have a zero base, then the translator does not even
generate an addition for the segment base.

Direct block chaining
---------------------

After each translated basic block is executed, QEMU uses the simulated
Program Counter (PC) and other cpu state information (such as the CS
segment base value) to find the next basic block.

In order to accelerate the most common cases where the new simulated PC
is known, QEMU can patch a basic block so that it jumps directly to the
next one.

The most portable code uses an indirect jump. An indirect jump makes
it easier to make the jump target modification atomic. On some host
architectures (such as x86 or PowerPC), the ``JUMP`` opcode is
directly patched so that the block chaining has no overhead.

Self-modifying code and translated code invalidation
----------------------------------------------------

Self-modifying code is a special challenge in x86 emulation because no
instruction cache invalidation is signaled by the application when code
is modified.

User-mode emulation marks a host page as write-protected (if it is
not already read-only) every time translated code is generated for a
basic block.  Then, if a write access is done to the page, Linux raises
a SEGV signal. QEMU then invalidates all the translated code in the page
and enables write accesses to the page.  For system emulation, write
protection is achieved through the software MMU.

Correct translated code invalidation is done efficiently by maintaining
a linked list of every translated block contained in a given page. Other
linked lists are also maintained to undo direct block chaining.

On RISC targets, correctly written software uses memory barriers and
cache flushes, so some of the protection above would not be
necessary. However, QEMU still requires that the generated code always
matches the target instructions in memory in order to handle
exceptions correctly.

Exception support
-----------------

longjmp() is used when an exception such as division by zero is
encountered.

The host SIGSEGV and SIGBUS signal handlers are used to get invalid
memory accesses.  QEMU keeps a map from host program counter to
target program counter, and looks up where the exception happened
based on the host program counter at the exception point.

On some targets, some bits of the virtual CPU's state are not flushed to the
memory until the end of the translation block.  This is done for internal
emulation state that is rarely accessed directly by the program and/or changes
very often throughout the execution of a translation block---this includes
condition codes on x86, delay slots on SPARC, conditional execution on
ARM, and so on.  This state is stored for each target instruction, and
looked up on exceptions.

MMU emulation
-------------

For system emulation QEMU uses a software MMU. In that mode, the MMU
virtual to physical address translation is done at every memory
access.

QEMU uses an address translation cache (TLB) to speed up the translation.
In order to avoid flushing the translated code each time the MMU
mappings change, all caches in QEMU are physically indexed.  This
means that each basic block is indexed with its physical address.

In order to avoid invalidating the basic block chain when MMU mappings
change, chaining is only performed when the destination of the jump
shares a page with the basic block that is performing the jump.

The MMU can also distinguish RAM and ROM memory areas from MMIO memory
areas.  Access is faster for RAM and ROM because the translation cache also
hosts the offset between guest address and host memory.  Accessing MMIO
memory areas instead calls out to C code for device emulation.
Finally, the MMU helps tracking dirty pages and pages pointed to by
translation blocks.

