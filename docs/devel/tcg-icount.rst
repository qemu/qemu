..
   Copyright (c) 2020, Linaro Limited
   Written by Alex Bennée


========================
TCG Instruction Counting
========================

TCG has long supported a feature known as icount which allows for
instruction counting during execution. This should not be confused
with cycle accurate emulation - QEMU does not attempt to emulate how
long an instruction would take on real hardware. That is a job for
other more detailed (and slower) tools that simulate the rest of a
micro-architecture.

This feature is only available for system emulation and is
incompatible with multi-threaded TCG. It can be used to better align
execution time with wall-clock time so a "slow" device doesn't run too
fast on modern hardware. It can also provides for a degree of
deterministic execution and is an essential part of the record/replay
support in QEMU.

Core Concepts
=============

At its heart icount is simply a count of executed instructions which
is stored in the TimersState of QEMU's timer sub-system. The number of
executed instructions can then be used to calculate QEMU_CLOCK_VIRTUAL
which represents the amount of elapsed time in the system since
execution started. Depending on the icount mode this may either be a
fixed number of ns per instruction or adjusted as execution continues
to keep wall clock time and virtual time in sync.

To be able to calculate the number of executed instructions the
translator starts by allocating a budget of instructions to be
executed. The budget of instructions is limited by how long it will be
until the next timer will expire. We store this budget as part of a
vCPU icount_decr field which shared with the machinery for handling
cpu_exit(). The whole field is checked at the start of every
translated block and will cause a return to the outer loop to deal
with whatever caused the exit.

In the case of icount, before the flag is checked we subtract the
number of instructions the translation block would execute. If this
would cause the instruction budget to go negative we exit the main
loop and regenerate a new translation block with exactly the right
number of instructions to take the budget to 0 meaning whatever timer
was due to expire will expire exactly when we exit the main run loop.

Dealing with MMIO
-----------------

While we can adjust the instruction budget for known events like timer
expiry we cannot do the same for MMIO. Every load/store we execute
might potentially trigger an I/O event, at which point we will need an
up to date and accurate reading of the icount number.

To deal with this case, when an I/O access is made we:

  - restore un-executed instructions to the icount budget
  - re-compile a single [1]_ instruction block for the current PC
  - exit the cpu loop and execute the re-compiled block

The new block is created with the CF_LAST_IO compile flag which
ensures the final instruction translation starts with a call to
gen_io_start() so we don't enter a perpetual loop constantly
recompiling a single instruction block. For translators using the
common translator_loop this is done automatically.
  
.. [1] sometimes two instructions if dealing with delay slots  

Other I/O operations
--------------------

MMIO isn't the only type of operation for which we might need a
correct and accurate clock. IO port instructions and accesses to
system registers are the common examples here. These instructions have
to be handled by the individual translators which have the knowledge
of which operations are I/O operations.

When the translator is handling an instruction of this kind:

* it must call gen_io_start() if icount is enabled, at some
   point before the generation of the code which actually does
   the I/O, using a code fragment similar to:

.. code:: c

    if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }

* it must end the TB immediately after this instruction

Note that some older front-ends call a "gen_io_end()" function:
this is obsolete and should not be used.
