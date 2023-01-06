.. _atomics-ref:

=========================
Atomic operations in QEMU
=========================

CPUs perform independent memory operations effectively in random order.
but this can be a problem for CPU-CPU interaction (including interactions
between QEMU and the guest).  Multi-threaded programs use various tools
to instruct the compiler and the CPU to restrict the order to something
that is consistent with the expectations of the programmer.

The most basic tool is locking.  Mutexes, condition variables and
semaphores are used in QEMU, and should be the default approach to
synchronization.  Anything else is considerably harder, but it's
also justified more often than one would like;
the most performance-critical parts of QEMU in particular require
a very low level approach to concurrency, involving memory barriers
and atomic operations.  The semantics of concurrent memory accesses are governed
by the C11 memory model.

QEMU provides a header, ``qemu/atomic.h``, which wraps C11 atomics to
provide better portability and a less verbose syntax.  ``qemu/atomic.h``
provides macros that fall in three camps:

- compiler barriers: ``barrier()``;

- weak atomic access and manual memory barriers: ``qatomic_read()``,
  ``qatomic_set()``, ``smp_rmb()``, ``smp_wmb()``, ``smp_mb()``,
  ``smp_mb_acquire()``, ``smp_mb_release()``, ``smp_read_barrier_depends()``;

- sequentially consistent atomic access: everything else.

In general, use of ``qemu/atomic.h`` should be wrapped with more easily
used data structures (e.g. the lock-free singly-linked list operations
``QSLIST_INSERT_HEAD_ATOMIC`` and ``QSLIST_MOVE_ATOMIC``) or synchronization
primitives (such as RCU, ``QemuEvent`` or ``QemuLockCnt``).  Bare use of
atomic operations and memory barriers should be limited to inter-thread
checking of flags and documented thoroughly.



Compiler memory barrier
=======================

``barrier()`` prevents the compiler from moving the memory accesses on
either side of it to the other side.  The compiler barrier has no direct
effect on the CPU, which may then reorder things however it wishes.

``barrier()`` is mostly used within ``qemu/atomic.h`` itself.  On some
architectures, CPU guarantees are strong enough that blocking compiler
optimizations already ensures the correct order of execution.  In this
case, ``qemu/atomic.h`` will reduce stronger memory barriers to simple
compiler barriers.

Still, ``barrier()`` can be useful when writing code that can be interrupted
by signal handlers.


Sequentially consistent atomic access
=====================================

Most of the operations in the ``qemu/atomic.h`` header ensure *sequential
consistency*, where "the result of any execution is the same as if the
operations of all the processors were executed in some sequential order,
and the operations of each individual processor appear in this sequence
in the order specified by its program".

``qemu/atomic.h`` provides the following set of atomic read-modify-write
operations::

    void qatomic_inc(ptr)
    void qatomic_dec(ptr)
    void qatomic_add(ptr, val)
    void qatomic_sub(ptr, val)
    void qatomic_and(ptr, val)
    void qatomic_or(ptr, val)

    typeof(*ptr) qatomic_fetch_inc(ptr)
    typeof(*ptr) qatomic_fetch_dec(ptr)
    typeof(*ptr) qatomic_fetch_add(ptr, val)
    typeof(*ptr) qatomic_fetch_sub(ptr, val)
    typeof(*ptr) qatomic_fetch_and(ptr, val)
    typeof(*ptr) qatomic_fetch_or(ptr, val)
    typeof(*ptr) qatomic_fetch_xor(ptr, val)
    typeof(*ptr) qatomic_fetch_inc_nonzero(ptr)
    typeof(*ptr) qatomic_xchg(ptr, val)
    typeof(*ptr) qatomic_cmpxchg(ptr, old, new)

all of which return the old value of ``*ptr``.  These operations are
polymorphic; they operate on any type that is as wide as a pointer or
smaller.

Similar operations return the new value of ``*ptr``::

    typeof(*ptr) qatomic_inc_fetch(ptr)
    typeof(*ptr) qatomic_dec_fetch(ptr)
    typeof(*ptr) qatomic_add_fetch(ptr, val)
    typeof(*ptr) qatomic_sub_fetch(ptr, val)
    typeof(*ptr) qatomic_and_fetch(ptr, val)
    typeof(*ptr) qatomic_or_fetch(ptr, val)
    typeof(*ptr) qatomic_xor_fetch(ptr, val)

``qemu/atomic.h`` also provides loads and stores that cannot be reordered
with each other::

    typeof(*ptr) qatomic_mb_read(ptr)
    void         qatomic_mb_set(ptr, val)

However these do not provide sequential consistency and, in particular,
they do not participate in the total ordering enforced by
sequentially-consistent operations.  For this reason they are deprecated.
They should instead be replaced with any of the following (ordered from
easiest to hardest):

- accesses inside a mutex or spinlock

- lightweight synchronization primitives such as ``QemuEvent``

- RCU operations (``qatomic_rcu_read``, ``qatomic_rcu_set``) when publishing
  or accessing a new version of a data structure

- other atomic accesses: ``qatomic_read`` and ``qatomic_load_acquire`` for
  loads, ``qatomic_set`` and ``qatomic_store_release`` for stores, ``smp_mb``
  to forbid reordering subsequent loads before a store.


Weak atomic access and manual memory barriers
=============================================

Compared to sequentially consistent atomic access, programming with
weaker consistency models can be considerably more complicated.
The only guarantees that you can rely upon in this case are:

- atomic accesses will not cause data races (and hence undefined behavior);
  ordinary accesses instead cause data races if they are concurrent with
  other accesses of which at least one is a write.  In order to ensure this,
  the compiler will not optimize accesses out of existence, create unsolicited
  accesses, or perform other similar optimzations.

- acquire operations will appear to happen, with respect to the other
  components of the system, before all the LOAD or STORE operations
  specified afterwards.

- release operations will appear to happen, with respect to the other
  components of the system, after all the LOAD or STORE operations
  specified before.

- release operations will *synchronize with* acquire operations;
  see :ref:`acqrel` for a detailed explanation.

When using this model, variables are accessed with:

- ``qatomic_read()`` and ``qatomic_set()``; these prevent the compiler from
  optimizing accesses out of existence and creating unsolicited
  accesses, but do not otherwise impose any ordering on loads and
  stores: both the compiler and the processor are free to reorder
  them.

- ``qatomic_load_acquire()``, which guarantees the LOAD to appear to
  happen, with respect to the other components of the system,
  before all the LOAD or STORE operations specified afterwards.
  Operations coming before ``qatomic_load_acquire()`` can still be
  reordered after it.

- ``qatomic_store_release()``, which guarantees the STORE to appear to
  happen, with respect to the other components of the system,
  after all the LOAD or STORE operations specified before.
  Operations coming after ``qatomic_store_release()`` can still be
  reordered before it.

Restrictions to the ordering of accesses can also be specified
using the memory barrier macros: ``smp_rmb()``, ``smp_wmb()``, ``smp_mb()``,
``smp_mb_acquire()``, ``smp_mb_release()``, ``smp_read_barrier_depends()``.

Memory barriers control the order of references to shared memory.
They come in six kinds:

- ``smp_rmb()`` guarantees that all the LOAD operations specified before
  the barrier will appear to happen before all the LOAD operations
  specified after the barrier with respect to the other components of
  the system.

  In other words, ``smp_rmb()`` puts a partial ordering on loads, but is not
  required to have any effect on stores.

- ``smp_wmb()`` guarantees that all the STORE operations specified before
  the barrier will appear to happen before all the STORE operations
  specified after the barrier with respect to the other components of
  the system.

  In other words, ``smp_wmb()`` puts a partial ordering on stores, but is not
  required to have any effect on loads.

- ``smp_mb_acquire()`` guarantees that all the LOAD operations specified before
  the barrier will appear to happen before all the LOAD or STORE operations
  specified after the barrier with respect to the other components of
  the system.

- ``smp_mb_release()`` guarantees that all the STORE operations specified *after*
  the barrier will appear to happen after all the LOAD or STORE operations
  specified *before* the barrier with respect to the other components of
  the system.

- ``smp_mb()`` guarantees that all the LOAD and STORE operations specified
  before the barrier will appear to happen before all the LOAD and
  STORE operations specified after the barrier with respect to the other
  components of the system.

  ``smp_mb()`` puts a partial ordering on both loads and stores.  It is
  stronger than both a read and a write memory barrier; it implies both
  ``smp_mb_acquire()`` and ``smp_mb_release()``, but it also prevents STOREs
  coming before the barrier from overtaking LOADs coming after the
  barrier and vice versa.

- ``smp_read_barrier_depends()`` is a weaker kind of read barrier.  On
  most processors, whenever two loads are performed such that the
  second depends on the result of the first (e.g., the first load
  retrieves the address to which the second load will be directed),
  the processor will guarantee that the first LOAD will appear to happen
  before the second with respect to the other components of the system.
  However, this is not always true---for example, it was not true on
  Alpha processors.  Whenever this kind of access happens to shared
  memory (that is not protected by a lock), a read barrier is needed,
  and ``smp_read_barrier_depends()`` can be used instead of ``smp_rmb()``.

  Note that the first load really has to have a _data_ dependency and not
  a control dependency.  If the address for the second load is dependent
  on the first load, but the dependency is through a conditional rather
  than actually loading the address itself, then it's a _control_
  dependency and a full read barrier or better is required.


Memory barriers and ``qatomic_load_acquire``/``qatomic_store_release`` are
mostly used when a data structure has one thread that is always a writer
and one thread that is always a reader:

    +----------------------------------+----------------------------------+
    | thread 1                         | thread 2                         |
    +==================================+==================================+
    | ::                               | ::                               |
    |                                  |                                  |
    |   qatomic_store_release(&a, x);  |   y = qatomic_load_acquire(&b);  |
    |   qatomic_store_release(&b, y);  |   x = qatomic_load_acquire(&a);  |
    +----------------------------------+----------------------------------+

In this case, correctness is easy to check for using the "pairing"
trick that is explained below.

Sometimes, a thread is accessing many variables that are otherwise
unrelated to each other (for example because, apart from the current
thread, exactly one other thread will read or write each of these
variables).  In this case, it is possible to "hoist" the barriers
outside a loop.  For example:

    +------------------------------------------+----------------------------------+
    | before                                   | after                            |
    +==========================================+==================================+
    | ::                                       | ::                               |
    |                                          |                                  |
    |   n = 0;                                 |   n = 0;                         |
    |   for (i = 0; i < 10; i++)               |   for (i = 0; i < 10; i++)       |
    |     n += qatomic_load_acquire(&a[i]);    |     n += qatomic_read(&a[i]);    |
    |                                          |   smp_mb_acquire();              |
    +------------------------------------------+----------------------------------+
    | ::                                       | ::                               |
    |                                          |                                  |
    |                                          |   smp_mb_release();              |
    |   for (i = 0; i < 10; i++)               |   for (i = 0; i < 10; i++)       |
    |     qatomic_store_release(&a[i], false); |     qatomic_set(&a[i], false);   |
    +------------------------------------------+----------------------------------+

Splitting a loop can also be useful to reduce the number of barriers:

    +------------------------------------------+----------------------------------+
    | before                                   | after                            |
    +==========================================+==================================+
    | ::                                       | ::                               |
    |                                          |                                  |
    |   n = 0;                                 |     smp_mb_release();            |
    |   for (i = 0; i < 10; i++) {             |     for (i = 0; i < 10; i++)     |
    |     qatomic_store_release(&a[i], false); |       qatomic_set(&a[i], false); |
    |     smp_mb();                            |     smb_mb();                    |
    |     n += qatomic_read(&b[i]);            |     n = 0;                       |
    |   }                                      |     for (i = 0; i < 10; i++)     |
    |                                          |       n += qatomic_read(&b[i]);  |
    +------------------------------------------+----------------------------------+

In this case, a ``smp_mb_release()`` is also replaced with a (possibly cheaper, and clearer
as well) ``smp_wmb()``:

    +------------------------------------------+----------------------------------+
    | before                                   | after                            |
    +==========================================+==================================+
    | ::                                       | ::                               |
    |                                          |                                  |
    |                                          |     smp_mb_release();            |
    |   for (i = 0; i < 10; i++) {             |     for (i = 0; i < 10; i++)     |
    |     qatomic_store_release(&a[i], false); |       qatomic_set(&a[i], false); |
    |     qatomic_store_release(&b[i], false); |     smb_wmb();                   |
    |   }                                      |     for (i = 0; i < 10; i++)     |
    |                                          |       qatomic_set(&b[i], false); |
    +------------------------------------------+----------------------------------+


.. _acqrel:

Acquire/release pairing and the *synchronizes-with* relation
------------------------------------------------------------

Atomic operations other than ``qatomic_set()`` and ``qatomic_read()`` have
either *acquire* or *release* semantics [#rmw]_.  This has two effects:

.. [#rmw] Read-modify-write operations can have both---acquire applies to the
          read part, and release to the write.

- within a thread, they are ordered either before subsequent operations
  (for acquire) or after previous operations (for release).

- if a release operation in one thread *synchronizes with* an acquire operation
  in another thread, the ordering constraints propagates from the first to the
  second thread.  That is, everything before the release operation in the
  first thread is guaranteed to *happen before* everything after the
  acquire operation in the second thread.

The concept of acquire and release semantics is not exclusive to atomic
operations; almost all higher-level synchronization primitives also have
acquire or release semantics.  For example:

- ``pthread_mutex_lock`` has acquire semantics, ``pthread_mutex_unlock`` has
  release semantics and synchronizes with a ``pthread_mutex_lock`` for the
  same mutex.

- ``pthread_cond_signal`` and ``pthread_cond_broadcast`` have release semantics;
  ``pthread_cond_wait`` has both release semantics (synchronizing with
  ``pthread_mutex_lock``) and acquire semantics (synchronizing with
  ``pthread_mutex_unlock`` and signaling of the condition variable).

- ``pthread_create`` has release semantics and synchronizes with the start
  of the new thread; ``pthread_join`` has acquire semantics and synchronizes
  with the exiting of the thread.

- ``qemu_event_set`` has release semantics, ``qemu_event_wait`` has
  acquire semantics.

For example, in the following example there are no atomic accesses, but still
thread 2 is relying on the *synchronizes-with* relation between ``pthread_exit``
(release) and ``pthread_join`` (acquire):

      +----------------------+-------------------------------+
      | thread 1             | thread 2                      |
      +======================+===============================+
      | ::                   | ::                            |
      |                      |                               |
      |   *a = 1;            |                               |
      |   pthread_exit(a);   |   pthread_join(thread1, &a);  |
      |                      |   x = *a;                     |
      +----------------------+-------------------------------+

Synchronization between threads basically descends from this pairing of
a release operation and an acquire operation.  Therefore, atomic operations
other than ``qatomic_set()`` and ``qatomic_read()`` will almost always be
paired with another operation of the opposite kind: an acquire operation
will pair with a release operation and vice versa.  This rule of thumb is
extremely useful; in the case of QEMU, however, note that the other
operation may actually be in a driver that runs in the guest!

``smp_read_barrier_depends()``, ``smp_rmb()``, ``smp_mb_acquire()``,
``qatomic_load_acquire()`` and ``qatomic_rcu_read()`` all count
as acquire operations.  ``smp_wmb()``, ``smp_mb_release()``,
``qatomic_store_release()`` and ``qatomic_rcu_set()`` all count as release
operations.  ``smp_mb()`` counts as both acquire and release, therefore
it can pair with any other atomic operation.  Here is an example:

      +----------------------+------------------------------+
      | thread 1             | thread 2                     |
      +======================+==============================+
      | ::                   | ::                           |
      |                      |                              |
      |   qatomic_set(&a, 1);|                              |
      |   smp_wmb();         |                              |
      |   qatomic_set(&b, 2);|   x = qatomic_read(&b);      |
      |                      |   smp_rmb();                 |
      |                      |   y = qatomic_read(&a);      |
      +----------------------+------------------------------+

Note that a load-store pair only counts if the two operations access the
same variable: that is, a store-release on a variable ``x`` *synchronizes
with* a load-acquire on a variable ``x``, while a release barrier
synchronizes with any acquire operation.  The following example shows
correct synchronization:

      +--------------------------------+--------------------------------+
      | thread 1                       | thread 2                       |
      +================================+================================+
      | ::                             | ::                             |
      |                                |                                |
      |   qatomic_set(&a, 1);          |                                |
      |   qatomic_store_release(&b, 2);|   x = qatomic_load_acquire(&b);|
      |                                |   y = qatomic_read(&a);        |
      +--------------------------------+--------------------------------+

Acquire and release semantics of higher-level primitives can also be
relied upon for the purpose of establishing the *synchronizes with*
relation.

Note that the "writing" thread is accessing the variables in the
opposite order as the "reading" thread.  This is expected: stores
before a release operation will normally match the loads after
the acquire operation, and vice versa.  In fact, this happened already
in the ``pthread_exit``/``pthread_join`` example above.

Finally, this more complex example has more than two accesses and data
dependency barriers.  It also does not use atomic accesses whenever there
cannot be a data race:

      +----------------------+------------------------------+
      | thread 1             | thread 2                     |
      +======================+==============================+
      | ::                   | ::                           |
      |                      |                              |
      |   b[2] = 1;          |                              |
      |   smp_wmb();         |                              |
      |   x->i = 2;          |                              |
      |   smp_wmb();         |                              |
      |   qatomic_set(&a, x);|  x = qatomic_read(&a);       |
      |                      |  smp_read_barrier_depends(); |
      |                      |  y = x->i;                   |
      |                      |  smp_read_barrier_depends(); |
      |                      |  z = b[y];                   |
      +----------------------+------------------------------+

Comparison with Linux kernel primitives
=======================================

Here is a list of differences between Linux kernel atomic operations
and memory barriers, and the equivalents in QEMU:

- atomic operations in Linux are always on a 32-bit int type and
  use a boxed ``atomic_t`` type; atomic operations in QEMU are polymorphic
  and use normal C types.

- Originally, ``atomic_read`` and ``atomic_set`` in Linux gave no guarantee
  at all. Linux 4.1 updated them to implement volatile
  semantics via ``ACCESS_ONCE`` (or the more recent ``READ``/``WRITE_ONCE``).

  QEMU's ``qatomic_read`` and ``qatomic_set`` implement C11 atomic relaxed
  semantics if the compiler supports it, and volatile semantics otherwise.
  Both semantics prevent the compiler from doing certain transformations;
  the difference is that atomic accesses are guaranteed to be atomic,
  while volatile accesses aren't. Thus, in the volatile case we just cross
  our fingers hoping that the compiler will generate atomic accesses,
  since we assume the variables passed are machine-word sized and
  properly aligned.

  No barriers are implied by ``qatomic_read`` and ``qatomic_set`` in either
  Linux or QEMU.

- atomic read-modify-write operations in Linux are of three kinds:

         ===================== =========================================
         ``atomic_OP``         returns void
         ``atomic_OP_return``  returns new value of the variable
         ``atomic_fetch_OP``   returns the old value of the variable
         ``atomic_cmpxchg``    returns the old value of the variable
         ===================== =========================================

  In QEMU, the second kind is named ``atomic_OP_fetch``.

- different atomic read-modify-write operations in Linux imply
  a different set of memory barriers; in QEMU, all of them enforce
  sequential consistency.

- in QEMU, ``qatomic_read()`` and ``qatomic_set()`` do not participate in
  the total ordering enforced by sequentially-consistent operations.
  This is because QEMU uses the C11 memory model.  The following example
  is correct in Linux but not in QEMU:

      +----------------------------------+--------------------------------+
      | Linux (correct)                  | QEMU (incorrect)               |
      +==================================+================================+
      | ::                               | ::                             |
      |                                  |                                |
      |   a = atomic_fetch_add(&x, 2);   |   a = qatomic_fetch_add(&x, 2);|
      |   b = READ_ONCE(&y);             |   b = qatomic_read(&y);        |
      +----------------------------------+--------------------------------+

  because the read of ``y`` can be moved (by either the processor or the
  compiler) before the write of ``x``.

  Fixing this requires an ``smp_mb()`` memory barrier between the write
  of ``x`` and the read of ``y``.  In the common case where only one thread
  writes ``x``, it is also possible to write it like this:

      +--------------------------------+
      | QEMU (correct)                 |
      +================================+
      | ::                             |
      |                                |
      |   a = qatomic_read(&x);        |
      |   qatomic_set(&x, a + 2);      |
      |   smp_mb();                    |
      |   b = qatomic_read(&y);        |
      +--------------------------------+

Sources
=======

- ``Documentation/memory-barriers.txt`` from the Linux kernel
