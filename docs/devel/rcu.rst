Using RCU (Read-Copy-Update) for synchronization
================================================

Read-copy update (RCU) is a synchronization mechanism that is used to
protect read-mostly data structures.  RCU is very efficient and scalable
on the read side (it is wait-free), and thus can make the read paths
extremely fast.

RCU supports concurrency between a single writer and multiple readers,
thus it is not used alone.  Typically, the write-side will use a lock to
serialize multiple updates, but other approaches are possible (e.g.,
restricting updates to a single task).  In QEMU, when a lock is used,
this will often be the "iothread mutex", also known as the "big QEMU
lock" (BQL).  Also, restricting updates to a single task is done in
QEMU using the "bottom half" API.

RCU is fundamentally a "wait-to-finish" mechanism.  The read side marks
sections of code with "critical sections", and the update side will wait
for the execution of all *currently running* critical sections before
proceeding, or before asynchronously executing a callback.

The key point here is that only the currently running critical sections
are waited for; critical sections that are started **after** the beginning
of the wait do not extend the wait, despite running concurrently with
the updater.  This is the reason why RCU is more scalable than,
for example, reader-writer locks.  It is so much more scalable that
the system will have a single instance of the RCU mechanism; a single
mechanism can be used for an arbitrary number of "things", without
having to worry about things such as contention or deadlocks.

How is this possible?  The basic idea is to split updates in two phases,
"removal" and "reclamation".  During removal, we ensure that subsequent
readers will not be able to get a reference to the old data.  After
removal has completed, a critical section will not be able to access
the old data.  Therefore, critical sections that begin after removal
do not matter; as soon as all previous critical sections have finished,
there cannot be any readers who hold references to the data structure,
and these can now be safely reclaimed (e.g., freed or unref'ed).

Here is a picture::

        thread 1                  thread 2                  thread 3
    -------------------    ------------------------    -------------------
    enter RCU crit.sec.
           |                finish removal phase
           |                begin wait
           |                      |                    enter RCU crit.sec.
    exit RCU crit.sec             |                           |
                            complete wait                     |
                            begin reclamation phase           |
                                                       exit RCU crit.sec.


Note how thread 3 is still executing its critical section when thread 2
starts reclaiming data.  This is possible, because the old version of the
data structure was not accessible at the time thread 3 began executing
that critical section.


RCU API
-------

The core RCU API is small:

``void rcu_read_lock(void);``
        Used by a reader to inform the reclaimer that the reader is
        entering an RCU read-side critical section.

``void rcu_read_unlock(void);``
        Used by a reader to inform the reclaimer that the reader is
        exiting an RCU read-side critical section.  Note that RCU
        read-side critical sections may be nested and/or overlapping.

``void synchronize_rcu(void);``
        Blocks until all pre-existing RCU read-side critical sections
        on all threads have completed.  This marks the end of the removal
        phase and the beginning of reclamation phase.

        Note that it would be valid for another update to come while
        ``synchronize_rcu`` is running.  Because of this, it is better that
        the updater releases any locks it may hold before calling
        ``synchronize_rcu``.  If this is not possible (for example, because
        the updater is protected by the BQL), you can use ``call_rcu``.

``void call_rcu1(struct rcu_head * head, void (*func)(struct rcu_head *head));``
        This function invokes ``func(head)`` after all pre-existing RCU
        read-side critical sections on all threads have completed.  This
        marks the end of the removal phase, with func taking care
        asynchronously of the reclamation phase.

        The ``foo`` struct needs to have an ``rcu_head`` structure added,
        perhaps as follows::

            struct foo {
                struct rcu_head rcu;
                int a;
                char b;
                long c;
            };

        so that the reclaimer function can fetch the ``struct foo`` address
        and free it::

            call_rcu1(&foo.rcu, foo_reclaim);

            void foo_reclaim(struct rcu_head *rp)
            {
                struct foo *fp = container_of(rp, struct foo, rcu);
                g_free(fp);
            }

        ``call_rcu1`` is typically used via either the ``call_rcu`` or
        ``g_free_rcu`` macros, which handle the common case where the
        ``rcu_head`` member is the first of the struct.

``void call_rcu(T *p, void (*func)(T *p), field-name);``
        If the ``struct rcu_head`` is the first field in the struct, you can
        use this macro instead of ``call_rcu1``.

``void g_free_rcu(T *p, field-name);``
        This is a special-case version of ``call_rcu`` where the callback
        function is ``g_free``.
        In the example given in ``call_rcu1``, one could have written simply::

            g_free_rcu(&foo, rcu);

``typeof(*p) qatomic_rcu_read(p);``
        ``qatomic_rcu_read()`` is similar to ``qatomic_load_acquire()``, but
        it makes some assumptions on the code that calls it.  This allows a
        more optimized implementation.

        ``qatomic_rcu_read`` assumes that whenever a single RCU critical
        section reads multiple shared data, these reads are either
        data-dependent or need no ordering.  This is almost always the
        case when using RCU, because read-side critical sections typically
        navigate one or more pointers (the pointers that are changed on
        every update) until reaching a data structure of interest,
        and then read from there.

        RCU read-side critical sections must use ``qatomic_rcu_read()`` to
        read data, unless concurrent writes are prevented by another
        synchronization mechanism.

        Furthermore, RCU read-side critical sections should traverse the
        data structure in a single direction, opposite to the direction
        in which the updater initializes it.

``void qatomic_rcu_set(p, typeof(*p) v);``
        ``qatomic_rcu_set()`` is similar to ``qatomic_store_release()``,
        though it also makes assumptions on the code that calls it in
        order to allow a more optimized implementation.

        In particular, ``qatomic_rcu_set()`` suffices for synchronization
        with readers, if the updater never mutates a field within a
        data item that is already accessible to readers.  This is the
        case when initializing a new copy of the RCU-protected data
        structure; just ensure that initialization of ``*p`` is carried out
        before ``qatomic_rcu_set()`` makes the data item visible to readers.
        If this rule is observed, writes will happen in the opposite
        order as reads in the RCU read-side critical sections (or if
        there is just one update), and there will be no need for other
        synchronization mechanism to coordinate the accesses.

The following APIs must be used before RCU is used in a thread:

``void rcu_register_thread(void);``
        Mark a thread as taking part in the RCU mechanism.  Such a thread
        will have to report quiescent points regularly, either manually
        or through the ``QemuCond``/``QemuSemaphore``/``QemuEvent`` APIs.

``void rcu_unregister_thread(void);``
        Mark a thread as not taking part anymore in the RCU mechanism.
        It is not a problem if such a thread reports quiescent points,
        either manually or by using the
        ``QemuCond``/``QemuSemaphore``/``QemuEvent`` APIs.

Note that these APIs are relatively heavyweight, and should **not** be
nested.

Convenience macros
------------------

Two macros are provided that automatically release the read lock at the
end of the scope.

``RCU_READ_LOCK_GUARD()``
         Takes the lock and will release it at the end of the block it's
         used in.

``WITH_RCU_READ_LOCK_GUARD()  { code }``
         Is used at the head of a block to protect the code within the block.

Note that a ``goto`` out of the guarded block will also drop the lock.

Differences with Linux
----------------------

- Waiting on a mutex is possible, though discouraged, within an RCU critical
  section.  This is because spinlocks are rarely (if ever) used in userspace
  programming; not allowing this would prevent upgrading an RCU read-side
  critical section to become an updater.

- ``qatomic_rcu_read`` and ``qatomic_rcu_set`` replace ``rcu_dereference`` and
  ``rcu_assign_pointer``.  They take a **pointer** to the variable being accessed.

- ``call_rcu`` is a macro that has an extra argument (the name of the first
  field in the struct, which must be a struct ``rcu_head``), and expects the
  type of the callback's argument to be the type of the first argument.
  ``call_rcu1`` is the same as Linux's ``call_rcu``.


RCU Patterns
------------

Many patterns using read-writer locks translate directly to RCU, with
the advantages of higher scalability and deadlock immunity.

In general, RCU can be used whenever it is possible to create a new
"version" of a data structure every time the updater runs.  This may
sound like a very strict restriction, however:

- the updater does not mean "everything that writes to a data structure",
  but rather "everything that involves a reclamation step".  See the
  array example below

- in some cases, creating a new version of a data structure may actually
  be very cheap.  For example, modifying the "next" pointer of a singly
  linked list is effectively creating a new version of the list.

Here are some frequently-used RCU idioms that are worth noting.


RCU list processing
^^^^^^^^^^^^^^^^^^^

TBD (not yet used in QEMU)


RCU reference counting
^^^^^^^^^^^^^^^^^^^^^^

Because grace periods are not allowed to complete while there is an RCU
read-side critical section in progress, the RCU read-side primitives
may be used as a restricted reference-counting mechanism.  For example,
consider the following code fragment::

    rcu_read_lock();
    p = qatomic_rcu_read(&foo);
    /* do something with p. */
    rcu_read_unlock();

The RCU read-side critical section ensures that the value of ``p`` remains
valid until after the ``rcu_read_unlock()``.  In some sense, it is acquiring
a reference to ``p`` that is later released when the critical section ends.
The write side looks simply like this (with appropriate locking)::

    qemu_mutex_lock(&foo_mutex);
    old = foo;
    qatomic_rcu_set(&foo, new);
    qemu_mutex_unlock(&foo_mutex);
    synchronize_rcu();
    free(old);

If the processing cannot be done purely within the critical section, it
is possible to combine this idiom with a "real" reference count::

    rcu_read_lock();
    p = qatomic_rcu_read(&foo);
    foo_ref(p);
    rcu_read_unlock();
    /* do something with p. */
    foo_unref(p);

The write side can be like this::

    qemu_mutex_lock(&foo_mutex);
    old = foo;
    qatomic_rcu_set(&foo, new);
    qemu_mutex_unlock(&foo_mutex);
    synchronize_rcu();
    foo_unref(old);

or with ``call_rcu``::

    qemu_mutex_lock(&foo_mutex);
    old = foo;
    qatomic_rcu_set(&foo, new);
    qemu_mutex_unlock(&foo_mutex);
    call_rcu(foo_unref, old, rcu);

In both cases, the write side only performs removal.  Reclamation
happens when the last reference to a ``foo`` object is dropped.
Using ``synchronize_rcu()`` is undesirably expensive, because the
last reference may be dropped on the read side.  Hence you can
use ``call_rcu()`` instead::

     foo_unref(struct foo *p) {
        if (qatomic_fetch_dec(&p->refcount) == 1) {
            call_rcu(foo_destroy, p, rcu);
        }
    }


Note that the same idioms would be possible with reader/writer
locks::

    read_lock(&foo_rwlock);         write_mutex_lock(&foo_rwlock);
    p = foo;                        p = foo;
    /* do something with p. */      foo = new;
    read_unlock(&foo_rwlock);       free(p);
                                    write_mutex_unlock(&foo_rwlock);
                                    free(p);

    ------------------------------------------------------------------

    read_lock(&foo_rwlock);         write_mutex_lock(&foo_rwlock);
    p = foo;                        old = foo;
    foo_ref(p);                     foo = new;
    read_unlock(&foo_rwlock);       foo_unref(old);
    /* do something with p. */      write_mutex_unlock(&foo_rwlock);
    read_lock(&foo_rwlock);
    foo_unref(p);
    read_unlock(&foo_rwlock);

``foo_unref`` could use a mechanism such as bottom halves to move deallocation
out of the write-side critical section.


RCU resizable arrays
^^^^^^^^^^^^^^^^^^^^

Resizable arrays can be used with RCU.  The expensive RCU synchronization
(or ``call_rcu``) only needs to take place when the array is resized.
The two items to take care of are:

- ensuring that the old version of the array is available between removal
  and reclamation;

- avoiding mismatches in the read side between the array data and the
  array size.

The first problem is avoided simply by not using ``realloc``.  Instead,
each resize will allocate a new array and copy the old data into it.
The second problem would arise if the size and the data pointers were
two members of a larger struct::

    struct mystuff {
        ...
        int data_size;
        int data_alloc;
        T   *data;
        ...
    };

Instead, we store the size of the array with the array itself::

    struct arr {
        int size;
        int alloc;
        T   data[];
    };
    struct arr *global_array;

    read side:
        rcu_read_lock();
        struct arr *array = qatomic_rcu_read(&global_array);
        x = i < array->size ? array->data[i] : -1;
        rcu_read_unlock();
        return x;

    write side (running under a lock):
        if (global_array->size == global_array->alloc) {
            /* Creating a new version.  */
            new_array = g_malloc(sizeof(struct arr) +
                                 global_array->alloc * 2 * sizeof(T));
            new_array->size = global_array->size;
            new_array->alloc = global_array->alloc * 2;
            memcpy(new_array->data, global_array->data,
                   global_array->alloc * sizeof(T));

            /* Removal phase.  */
            old_array = global_array;
            qatomic_rcu_set(&global_array, new_array);
            synchronize_rcu();

            /* Reclamation phase.  */
            free(old_array);
        }


References
----------

* The `Linux kernel RCU documentation <https://docs.kernel.org/RCU/>`__
