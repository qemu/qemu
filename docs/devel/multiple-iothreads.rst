Using Multiple ``IOThread``\ s
==============================

..
   Copyright (c) 2014-2017 Red Hat Inc.

   This work is licensed under the terms of the GNU GPL, version 2 or later.  See
   the COPYING file in the top-level directory.


This document explains the ``IOThread`` feature and how to write code that runs
outside the BQL.

The main loop and ``IOThread``\ s
---------------------------------
QEMU is an event-driven program that can do several things at once using an
event loop.  The VNC server and the QMP monitor are both processed from the
same event loop, which monitors their file descriptors until they become
readable and then invokes a callback.

The default event loop is called the main loop (see ``main-loop.c``).  It is
possible to create additional event loop threads using
``-object iothread,id=my-iothread``.

Side note: The main loop and ``IOThread`` are both event loops but their code is
not shared completely.  Sometimes it is useful to remember that although they
are conceptually similar they are currently not interchangeable.

Why ``IOThread``\ s are useful
------------------------------
``IOThread``\ s allow the user to control the placement of work.  The main loop is a
scalability bottleneck on hosts with many CPUs.  Work can be spread across
several ``IOThread``\ s instead of just one main loop.  When set up correctly this
can improve I/O latency and reduce jitter seen by the guest.

The main loop is also deeply associated with the BQL, which is a
scalability bottleneck in itself.  vCPU threads and the main loop use the BQL
to serialize execution of QEMU code.  This mutex is necessary because a lot of
QEMU's code historically was not thread-safe.

The fact that all I/O processing is done in a single main loop and that the
BQL is contended by all vCPU threads and the main loop explain
why it is desirable to place work into ``IOThread``\ s.

The experimental ``virtio-blk`` data-plane implementation has been benchmarked and
shows these effects:
ftp://public.dhe.ibm.com/linux/pdfs/KVM_Virtualized_IO_Performance_Paper.pdf

.. _how-to-program:

How to program for ``IOThread``\ s
----------------------------------
The main difference between legacy code and new code that can run in an
``IOThread`` is dealing explicitly with the event loop object, ``AioContext``
(see ``include/block/aio.h``).  Code that only works in the main loop
implicitly uses the main loop's ``AioContext``.  Code that supports running
in ``IOThread``\ s must be aware of its ``AioContext``.

AioContext supports the following services:
 * File descriptor monitoring (read/write/error on POSIX hosts)
 * Event notifiers (inter-thread signalling)
 * Timers
 * Bottom Halves (BH) deferred callbacks

There are several old APIs that use the main loop AioContext:
 * LEGACY ``qemu_aio_set_fd_handler()`` - monitor a file descriptor
 * LEGACY ``qemu_aio_set_event_notifier()`` - monitor an event notifier
 * LEGACY ``timer_new_ms()`` - create a timer
 * LEGACY ``qemu_bh_new()`` - create a BH
 * LEGACY ``qemu_bh_new_guarded()`` - create a BH with a device re-entrancy guard
 * LEGACY ``qemu_aio_wait()`` - run an event loop iteration

Since they implicitly work on the main loop they cannot be used in code that
runs in an ``IOThread``.  They might cause a crash or deadlock if called from an
``IOThread`` since the BQL is not held.

Instead, use the ``AioContext`` functions directly (see ``include/block/aio.h``):
 * ``aio_set_fd_handler()`` - monitor a file descriptor
 * ``aio_set_event_notifier()`` - monitor an event notifier
 * ``aio_timer_new()`` - create a timer
 * ``aio_bh_new()`` - create a BH
 * ``aio_bh_new_guarded()`` - create a BH with a device re-entrancy guard
 * ``aio_poll()`` - run an event loop iteration

The ``qemu_bh_new_guarded``/``aio_bh_new_guarded`` APIs accept a
``MemReentrancyGuard``
argument, which is used to check for and prevent re-entrancy problems. For
BHs associated with devices, the reentrancy-guard is contained in the
corresponding ``DeviceState`` and named ``mem_reentrancy_guard``.

The ``AioContext`` can be obtained from the ``IOThread`` using
``iothread_get_aio_context()`` or for the main loop using
``qemu_get_aio_context()``. Code that takes an ``AioContext`` argument
works both in ``IOThread``\ s or the main loop, depending on which ``AioContext``
instance the caller passes in.

How to synchronize with an ``IOThread``
---------------------------------------
Variables that can be accessed by multiple threads require some form of
synchronization such as ``qemu_mutex_lock()``, ``rcu_read_lock()``, etc.

``AioContext`` functions like ``aio_set_fd_handler()``,
``aio_set_event_notifier()``, ``aio_bh_new()``, and ``aio_timer_new()``
are thread-safe. They can be used to trigger activity in an ``IOThread``.

Side note: the best way to schedule a function call across threads is to call
``aio_bh_schedule_oneshot()``.

The main loop thread can wait synchronously for a condition using
``AIO_WAIT_WHILE()``.

``AioContext`` and the block layer
----------------------------------
The ``AioContext`` originates from the QEMU block layer, even though nowadays
``AioContext`` is a generic event loop that can be used by any QEMU subsystem.

The block layer has support for ``AioContext`` integrated.  Each
``BlockDriverState`` is associated with an ``AioContext`` using
``bdrv_try_change_aio_context()`` and ``bdrv_get_aio_context()``.
This allows block layer code to process I/O inside the
right ``AioContext``.  Other subsystems may wish to follow a similar approach.

Block layer code must therefore expect to run in an ``IOThread`` and avoid using
old APIs that implicitly use the main loop.  See
`How to program for IOThreads`_ for information on how to do that.

Code running in the monitor typically needs to ensure that past
requests from the guest are completed.  When a block device is running
in an ``IOThread``, the ``IOThread`` can also process requests from the guest
(via ioeventfd).  To achieve both objects, wrap the code between
``bdrv_drained_begin()`` and ``bdrv_drained_end()``, thus creating a "drained
section".

Long-running jobs (usually in the form of coroutines) are often scheduled in
the ``BlockDriverState``'s ``AioContext``.  The functions
``bdrv_add``/``remove_aio_context_notifier``, or alternatively
``blk_add``/``remove_aio_context_notifier`` if you use ``BlockBackends``,
can be used to get a notification whenever ``bdrv_try_change_aio_context()``
moves a ``BlockDriverState`` to a different ``AioContext``.
