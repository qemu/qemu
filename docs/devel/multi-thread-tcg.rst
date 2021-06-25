..
  Copyright (c) 2015-2020 Linaro Ltd.

  This work is licensed under the terms of the GNU GPL, version 2 or
  later. See the COPYING file in the top-level directory.

==================
Multi-threaded TCG
==================

This document outlines the design for multi-threaded TCG (a.k.a MTTCG)
system-mode emulation. user-mode emulation has always mirrored the
thread structure of the translated executable although some of the
changes done for MTTCG system emulation have improved the stability of
linux-user emulation.

The original system-mode TCG implementation was single threaded and
dealt with multiple CPUs with simple round-robin scheduling. This
simplified a lot of things but became increasingly limited as systems
being emulated gained additional cores and per-core performance gains
for host systems started to level off.

vCPU Scheduling
===============

We introduce a new running mode where each vCPU will run on its own
user-space thread. This is enabled by default for all FE/BE
combinations where the host memory model is able to accommodate the
guest (TCG_GUEST_DEFAULT_MO & ~TCG_TARGET_DEFAULT_MO is zero) and the
guest has had the required work done to support this safely
(TARGET_SUPPORTS_MTTCG).

System emulation will fall back to the original round robin approach
if:

* forced by --accel tcg,thread=single
* enabling --icount mode
* 64 bit guests on 32 bit hosts (TCG_OVERSIZED_GUEST)

In the general case of running translated code there should be no
inter-vCPU dependencies and all vCPUs should be able to run at full
speed. Synchronisation will only be required while accessing internal
shared data structures or when the emulated architecture requires a
coherent representation of the emulated machine state.

Shared Data Structures
======================

Main Run Loop
-------------

Even when there is no code being generated there are a number of
structures associated with the hot-path through the main run-loop.
These are associated with looking up the next translation block to
execute. These include:

    tb_jmp_cache (per-vCPU, cache of recent jumps)
    tb_ctx.htable (global hash table, phys address->tb lookup)

As TB linking only occurs when blocks are in the same page this code
is critical to performance as looking up the next TB to execute is the
most common reason to exit the generated code.

DESIGN REQUIREMENT: Make access to lookup structures safe with
multiple reader/writer threads. Minimise any lock contention to do it.

The hot-path avoids using locks where possible. The tb_jmp_cache is
updated with atomic accesses to ensure consistent results. The fall
back QHT based hash table is also designed for lockless lookups. Locks
are only taken when code generation is required or TranslationBlocks
have their block-to-block jumps patched.

Global TCG State
----------------

User-mode emulation
~~~~~~~~~~~~~~~~~~~

We need to protect the entire code generation cycle including any post
generation patching of the translated code. This also implies a shared
translation buffer which contains code running on all cores. Any
execution path that comes to the main run loop will need to hold a
mutex for code generation. This also includes times when we need flush
code or entries from any shared lookups/caches. Structures held on a
per-vCPU basis won't need locking unless other vCPUs will need to
modify them.

DESIGN REQUIREMENT: Add locking around all code generation and TB
patching.

(Current solution)

Code generation is serialised with mmap_lock().

!User-mode emulation
~~~~~~~~~~~~~~~~~~~~

Each vCPU has its own TCG context and associated TCG region, thereby
requiring no locking during translation.

Translation Blocks
------------------

Currently the whole system shares a single code generation buffer
which when full will force a flush of all translations and start from
scratch again. Some operations also force a full flush of translations
including:

  - debugging operations (breakpoint insertion/removal)
  - some CPU helper functions
  - linux-user spawning its first thread

This is done with the async_safe_run_on_cpu() mechanism to ensure all
vCPUs are quiescent when changes are being made to shared global
structures.

More granular translation invalidation events are typically due
to a change of the state of a physical page:

  - code modification (self modify code, patching code)
  - page changes (new page mapping in linux-user mode)

While setting the invalid flag in a TranslationBlock will stop it
being used when looked up in the hot-path there are a number of other
book-keeping structures that need to be safely cleared.

Any TranslationBlocks which have been patched to jump directly to the
now invalid blocks need the jump patches reversing so they will return
to the C code.

There are a number of look-up caches that need to be properly updated
including the:

  - jump lookup cache
  - the physical-to-tb lookup hash table
  - the global page table

The global page table (l1_map) which provides a multi-level look-up
for PageDesc structures which contain pointers to the start of a
linked list of all Translation Blocks in that page (see page_next).

Both the jump patching and the page cache involve linked lists that
the invalidated TranslationBlock needs to be removed from.

DESIGN REQUIREMENT: Safely handle invalidation of TBs
                      - safely patch/revert direct jumps
                      - remove central PageDesc lookup entries
                      - ensure lookup caches/hashes are safely updated

(Current solution)

The direct jump themselves are updated atomically by the TCG
tb_set_jmp_target() code. Modification to the linked lists that allow
searching for linked pages are done under the protection of tb->jmp_lock,
where tb is the destination block of a jump. Each origin block keeps a
pointer to its destinations so that the appropriate lock can be acquired before
iterating over a jump list.

The global page table is a lockless radix tree; cmpxchg is used
to atomically insert new elements.

The lookup caches are updated atomically and the lookup hash uses QHT
which is designed for concurrent safe lookup.

Parallel code generation is supported. QHT is used at insertion time
as the synchronization point across threads, thereby ensuring that we only
keep track of a single TranslationBlock for each guest code block.

Memory maps and TLBs
--------------------

The memory handling code is fairly critical to the speed of memory
access in the emulated system. The SoftMMU code is designed so the
hot-path can be handled entirely within translated code. This is
handled with a per-vCPU TLB structure which once populated will allow
a series of accesses to the page to occur without exiting the
translated code. It is possible to set flags in the TLB address which
will ensure the slow-path is taken for each access. This can be done
to support:

  - Memory regions (dividing up access to PIO, MMIO and RAM)
  - Dirty page tracking (for code gen, SMC detection, migration and display)
  - Virtual TLB (for translating guest address->real address)

When the TLB tables are updated by a vCPU thread other than their own
we need to ensure it is done in a safe way so no inconsistent state is
seen by the vCPU thread.

Some operations require updating a number of vCPUs TLBs at the same
time in a synchronised manner.

DESIGN REQUIREMENTS:

  - TLB Flush All/Page
    - can be across-vCPUs
    - cross vCPU TLB flush may need other vCPU brought to halt
    - change may need to be visible to the calling vCPU immediately
  - TLB Flag Update
    - usually cross-vCPU
    - want change to be visible as soon as possible
  - TLB Update (update a CPUTLBEntry, via tlb_set_page_with_attrs)
    - This is a per-vCPU table - by definition can't race
    - updated by its own thread when the slow-path is forced

(Current solution)

We have updated cputlb.c to defer operations when a cross-vCPU
operation with async_run_on_cpu() which ensures each vCPU sees a
coherent state when it next runs its work (in a few instructions
time).

A new set up operations (tlb_flush_*_all_cpus) take an additional flag
which when set will force synchronisation by setting the source vCPUs
work as "safe work" and exiting the cpu run loop. This ensure by the
time execution restarts all flush operations have completed.

TLB flag updates are all done atomically and are also protected by the
corresponding page lock.

(Known limitation)

Not really a limitation but the wait mechanism is overly strict for
some architectures which only need flushes completed by a barrier
instruction. This could be a future optimisation.

Emulated hardware state
-----------------------

Currently thanks to KVM work any access to IO memory is automatically
protected by the global iothread mutex, also known as the BQL (Big
Qemu Lock). Any IO region that doesn't use global mutex is expected to
do its own locking.

However IO memory isn't the only way emulated hardware state can be
modified. Some architectures have model specific registers that
trigger hardware emulation features. Generally any translation helper
that needs to update more than a single vCPUs of state should take the
BQL.

As the BQL, or global iothread mutex is shared across the system we
push the use of the lock as far down into the TCG code as possible to
minimise contention.

(Current solution)

MMIO access automatically serialises hardware emulation by way of the
BQL. Currently Arm targets serialise all ARM_CP_IO register accesses
and also defer the reset/startup of vCPUs to the vCPU context by way
of async_run_on_cpu().

Updates to interrupt state are also protected by the BQL as they can
often be cross vCPU.

Memory Consistency
==================

Between emulated guests and host systems there are a range of memory
consistency models. Even emulating weakly ordered systems on strongly
ordered hosts needs to ensure things like store-after-load re-ordering
can be prevented when the guest wants to.

Memory Barriers
---------------

Barriers (sometimes known as fences) provide a mechanism for software
to enforce a particular ordering of memory operations from the point
of view of external observers (e.g. another processor core). They can
apply to any memory operations as well as just loads or stores.

The Linux kernel has an excellent `write-up
<https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/plain/Documentation/memory-barriers.txt>`_
on the various forms of memory barrier and the guarantees they can
provide.

Barriers are often wrapped around synchronisation primitives to
provide explicit memory ordering semantics. However they can be used
by themselves to provide safe lockless access by ensuring for example
a change to a signal flag will only be visible once the changes to
payload are.

DESIGN REQUIREMENT: Add a new tcg_memory_barrier op

This would enforce a strong load/store ordering so all loads/stores
complete at the memory barrier. On single-core non-SMP strongly
ordered backends this could become a NOP.

Aside from explicit standalone memory barrier instructions there are
also implicit memory ordering semantics which comes with each guest
memory access instruction. For example all x86 load/stores come with
fairly strong guarantees of sequential consistency whereas Arm has
special variants of load/store instructions that imply acquire/release
semantics.

In the case of a strongly ordered guest architecture being emulated on
a weakly ordered host the scope for a heavy performance impact is
quite high.

DESIGN REQUIREMENTS: Be efficient with use of memory barriers
       - host systems with stronger implied guarantees can skip some barriers
       - merge consecutive barriers to the strongest one

(Current solution)

The system currently has a tcg_gen_mb() which will add memory barrier
operations if code generation is being done in a parallel context. The
tcg_optimize() function attempts to merge barriers up to their
strongest form before any load/store operations. The solution was
originally developed and tested for linux-user based systems. All
backends have been converted to emit fences when required. So far the
following front-ends have been updated to emit fences when required:

    - target-i386
    - target-arm
    - target-aarch64
    - target-alpha
    - target-mips

Memory Control and Maintenance
------------------------------

This includes a class of instructions for controlling system cache
behaviour. While QEMU doesn't model cache behaviour these instructions
are often seen when code modification has taken place to ensure the
changes take effect.

Synchronisation Primitives
--------------------------

There are two broad types of synchronisation primitives found in
modern ISAs: atomic instructions and exclusive regions.

The first type offer a simple atomic instruction which will guarantee
some sort of test and conditional store will be truly atomic w.r.t.
other cores sharing access to the memory. The classic example is the
x86 cmpxchg instruction.

The second type offer a pair of load/store instructions which offer a
guarantee that a region of memory has not been touched between the
load and store instructions. An example of this is Arm's ldrex/strex
pair where the strex instruction will return a flag indicating a
successful store only if no other CPU has accessed the memory region
since the ldrex.

Traditionally TCG has generated a series of operations that work
because they are within the context of a single translation block so
will have completed before another CPU is scheduled. However with
the ability to have multiple threads running to emulate multiple CPUs
we will need to explicitly expose these semantics.

DESIGN REQUIREMENTS:
  - Support classic atomic instructions
  - Support load/store exclusive (or load link/store conditional) pairs
  - Generic enough infrastructure to support all guest architectures
CURRENT OPEN QUESTIONS:
  - How problematic is the ABA problem in general?

(Current solution)

The TCG provides a number of atomic helpers (tcg_gen_atomic_*) which
can be used directly or combined to emulate other instructions like
Arm's ldrex/strex instructions. While they are susceptible to the ABA
problem so far common guests have not implemented patterns where
this may be a problem - typically presenting a locking ABI which
assumes cmpxchg like semantics.

The code also includes a fall-back for cases where multi-threaded TCG
ops can't work (e.g. guest atomic width > host atomic width). In this
case an EXCP_ATOMIC exit occurs and the instruction is emulated with
an exclusive lock which ensures all emulation is serialised.

While the atomic helpers look good enough for now there may be a need
to look at solutions that can more closely model the guest
architectures semantics.
