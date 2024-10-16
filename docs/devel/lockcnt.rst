Locked Counters (aka ``QemuLockCnt``)
=====================================

QEMU often uses reference counts to track data structures that are being
accessed and should not be freed.  For example, a loop that invoke
callbacks like this is not safe::

    QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
        if (ioh->revents & G_IO_OUT) {
            ioh->fd_write(ioh->opaque);
        }
    }

``QLIST_FOREACH_SAFE`` protects against deletion of the current node (``ioh``)
by stashing away its ``next`` pointer.  However, ``ioh->fd_write`` could
actually delete the next node from the list.  The simplest way to
avoid this is to mark the node as deleted, and remove it from the
list in the above loop::

    QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
        if (ioh->deleted) {
            QLIST_REMOVE(ioh, next);
            g_free(ioh);
        } else {
            if (ioh->revents & G_IO_OUT) {
                ioh->fd_write(ioh->opaque);
            }
        }
    }

If however this loop must also be reentrant, i.e. it is possible that
``ioh->fd_write`` invokes the loop again, some kind of counting is needed::

    walking_handlers++;
    QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
        if (ioh->deleted) {
            if (walking_handlers == 1) {
                QLIST_REMOVE(ioh, next);
                g_free(ioh);
            }
        } else {
            if (ioh->revents & G_IO_OUT) {
                ioh->fd_write(ioh->opaque);
            }
        }
    }
    walking_handlers--;

One may think of using the RCU primitives, ``rcu_read_lock()`` and
``rcu_read_unlock()``; effectively, the RCU nesting count would take
the place of the walking_handlers global variable.  Indeed,
reference counting and RCU have similar purposes, but their usage in
general is complementary:

- reference counting is fine-grained and limited to a single data
  structure; RCU delays reclamation of *all* RCU-protected data
  structures;

- reference counting works even in the presence of code that keeps
  a reference for a long time; RCU critical sections in principle
  should be kept short;

- reference counting is often applied to code that is not thread-safe
  but is reentrant; in fact, usage of reference counting in QEMU predates
  the introduction of threads by many years.  RCU is generally used to
  protect readers from other threads freeing memory after concurrent
  modifications to a data structure.

- reclaiming data can be done by a separate thread in the case of RCU;
  this can improve performance, but also delay reclamation undesirably.
  With reference counting, reclamation is deterministic.

This file documents ``QemuLockCnt``, an abstraction for using reference
counting in code that has to be both thread-safe and reentrant.


``QemuLockCnt`` concepts
------------------------

A ``QemuLockCnt`` comprises both a counter and a mutex; it has primitives
to increment and decrement the counter, and to take and release the
mutex.  The counter notes how many visits to the data structures are
taking place (the visits could be from different threads, or there could
be multiple reentrant visits from the same thread).  The basic rules
governing the counter/mutex pair then are the following:

- Data protected by the QemuLockCnt must not be freed unless the
  counter is zero and the mutex is taken.

- A new visit cannot be started while the counter is zero and the
  mutex is taken.

Most of the time, the mutex protects all writes to the data structure,
not just frees, though there could be cases where this is not necessary.

Reads, instead, can be done without taking the mutex, as long as the
readers and writers use the same macros that are used for RCU, for
example ``qatomic_rcu_read``, ``qatomic_rcu_set``, ``QLIST_FOREACH_RCU``,
etc.  This is because the reads are done outside a lock and a set
or ``QLIST_INSERT_HEAD``
can happen concurrently with the read.  The RCU API ensures that the
processor and the compiler see all required memory barriers.

This could be implemented simply by protecting the counter with the
mutex, for example::

    // (1)
    qemu_mutex_lock(&walking_handlers_mutex);
    walking_handlers++;
    qemu_mutex_unlock(&walking_handlers_mutex);

    ...

    // (2)
    qemu_mutex_lock(&walking_handlers_mutex);
    if (--walking_handlers == 0) {
        QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
            if (ioh->deleted) {
                QLIST_REMOVE(ioh, next);
                g_free(ioh);
            }
        }
    }
    qemu_mutex_unlock(&walking_handlers_mutex);

Here, no frees can happen in the code represented by the ellipsis.
If another thread is executing critical section (2), that part of
the code cannot be entered, because the thread will not be able
to increment the ``walking_handlers`` variable.  And of course
during the visit any other thread will see a nonzero value for
``walking_handlers``, as in the single-threaded code.

Note that it is possible for multiple concurrent accesses to delay
the cleanup arbitrarily; in other words, for the ``walking_handlers``
counter to never become zero.  For this reason, this technique is
more easily applicable if concurrent access to the structure is rare.

However, critical sections are easy to forget since you have to do
them for each modification of the counter.  ``QemuLockCnt`` ensures that
all modifications of the counter take the lock appropriately, and it
can also be more efficient in two ways:

- it avoids taking the lock for many operations (for example
  incrementing the counter while it is non-zero);

- on some platforms, one can implement ``QemuLockCnt`` to hold the lock
  and the mutex in a single word, making the fast path no more expensive
  than simply managing a counter using atomic operations (see
  :doc:`atomics`).  This can be very helpful if concurrent access to
  the data structure is expected to be rare.


Using the same mutex for frees and writes can still incur some small
inefficiencies; for example, a visit can never start if the counter is
zero and the mutex is taken -- even if the mutex is taken by a write,
which in principle need not block a visit of the data structure.
However, these are usually not a problem if any of the following
assumptions are valid:

- concurrent access is possible but rare

- writes are rare

- writes are frequent, but this kind of write (e.g. appending to a
  list) has a very small critical section.

For example, QEMU uses ``QemuLockCnt`` to manage an ``AioContext``'s list of
bottom halves and file descriptor handlers.  Modifications to the list
of file descriptor handlers are rare.  Creation of a new bottom half is
frequent and can happen on a fast path; however: 1) it is almost never
concurrent with a visit to the list of bottom halves; 2) it only has
three instructions in the critical path, two assignments and a ``smp_wmb()``.


``QemuLockCnt`` API
-------------------

.. kernel-doc:: include/qemu/lockcnt.h


``QemuLockCnt`` usage
---------------------

This section explains the typical usage patterns for ``QemuLockCnt`` functions.

Setting a variable to a non-NULL value can be done between
``qemu_lockcnt_lock`` and ``qemu_lockcnt_unlock``::

    qemu_lockcnt_lock(&xyz_lockcnt);
    if (!xyz) {
        new_xyz = g_new(XYZ, 1);
        ...
        qatomic_rcu_set(&xyz, new_xyz);
    }
    qemu_lockcnt_unlock(&xyz_lockcnt);

Accessing the value can be done between ``qemu_lockcnt_inc`` and
``qemu_lockcnt_dec``::

    qemu_lockcnt_inc(&xyz_lockcnt);
    if (xyz) {
        XYZ *p = qatomic_rcu_read(&xyz);
        ...
        /* Accesses can now be done through "p".  */
    }
    qemu_lockcnt_dec(&xyz_lockcnt);

Freeing the object can similarly use ``qemu_lockcnt_lock`` and
``qemu_lockcnt_unlock``, but you also need to ensure that the count
is zero (i.e. there is no concurrent visit).  Because ``qemu_lockcnt_inc``
takes the ``QemuLockCnt``'s lock, the count cannot become non-zero while
the object is being freed.  Freeing an object looks like this::

    qemu_lockcnt_lock(&xyz_lockcnt);
    if (!qemu_lockcnt_count(&xyz_lockcnt)) {
        g_free(xyz);
        xyz = NULL;
    }
    qemu_lockcnt_unlock(&xyz_lockcnt);

If an object has to be freed right after a visit, you can combine
the decrement, the locking and the check on count as follows::

    qemu_lockcnt_inc(&xyz_lockcnt);
    if (xyz) {
        XYZ *p = qatomic_rcu_read(&xyz);
        ...
        /* Accesses can now be done through "p".  */
    }
    if (qemu_lockcnt_dec_and_lock(&xyz_lockcnt)) {
        g_free(xyz);
        xyz = NULL;
        qemu_lockcnt_unlock(&xyz_lockcnt);
    }

``QemuLockCnt`` can also be used to access a list as follows::

    qemu_lockcnt_inc(&io_handlers_lockcnt);
    QLIST_FOREACH_RCU(ioh, &io_handlers, pioh) {
        if (ioh->revents & G_IO_OUT) {
            ioh->fd_write(ioh->opaque);
        }
    }

    if (qemu_lockcnt_dec_and_lock(&io_handlers_lockcnt)) {
        QLIST_FOREACH_SAFE(ioh, &io_handlers, next, pioh) {
            if (ioh->deleted) {
                QLIST_REMOVE(ioh, next);
                g_free(ioh);
            }
        }
        qemu_lockcnt_unlock(&io_handlers_lockcnt);
    }

Again, the RCU primitives are used because new items can be added to the
list during the walk.  ``QLIST_FOREACH_RCU`` ensures that the processor and
the compiler see the appropriate memory barriers.

An alternative pattern uses ``qemu_lockcnt_dec_if_lock``::

    qemu_lockcnt_inc(&io_handlers_lockcnt);
    QLIST_FOREACH_SAFE_RCU(ioh, &io_handlers, next, pioh) {
        if (ioh->deleted) {
            if (qemu_lockcnt_dec_if_lock(&io_handlers_lockcnt)) {
                QLIST_REMOVE(ioh, next);
                g_free(ioh);
                qemu_lockcnt_inc_and_unlock(&io_handlers_lockcnt);
            }
        } else {
            if (ioh->revents & G_IO_OUT) {
                ioh->fd_write(ioh->opaque);
            }
        }
    }
    qemu_lockcnt_dec(&io_handlers_lockcnt);

Here you can use ``qemu_lockcnt_dec`` instead of ``qemu_lockcnt_dec_and_lock``,
because there is no special task to do if the count goes from 1 to 0.
