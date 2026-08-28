==================
Fast Snapshot Load
==================

Overview
========
Fast snapshot load is an extension of the postcopy migration feature
to disk loads.

Unlike a usual snapshot load, which requires all VM data (RAM as well
as device states) to be loaded into host RAM from the snapshot file
for the guest to run, fast snapshot load uses postcopy infrastructure
to load in only the required device states and allows RAM pages to
be loaded after the guest starts execution. The idea is to start the
guest and serve its page faults on the go, reducing the perceived
resume time for large snapshots.

Architecture
============
This feature combines postcopy migration and mapped-ram capabilities
to load RAM pages on demand. It is done by catching guest faults using
Linux ``userfaultfd`` and loading the page by calculating the offset
of its location in the snapshot file using mapped-ram capabilities.

Fault Thread
------------
The fault thread uses Linux ``userfaultfd`` to catch page faults caused
by guest and directly load the page from the snapshot file. It is
very similar to network postcopy fault thread, with primary difference
being it loads pages directly by reading from the snapshot file.

Eager Thread
------------
Eager thread iterates over all pages in RAM and loads each page not
yet loaded by fault thread. It is required as unlike network postcopy
where majority of RAM has already been loaded via precopy, here entire
RAM is waiting to be loaded. If there is no eager loading thread each
page will only be loaded when it is required by guest. In case there
are some background pages that are never/rarely accessed by guest,
the system will be locked in migration state indefinitely.

Synchronization
---------------
In order to make sure both of these threads do not load the same page
twice potentially overwriting and corrupting user RAM, a bitmap is
used (``RAMBlock->pending_bmap``) which tracks the pages claimed to
be loaded by threads. This prevents race condition when one thread
is loading the page and other one tries to do the same.

Usage
=====

Simply enable ``mapped-ram`` and ``postcopy-ram`` capabilities on
the destination:

.. code-block:: text

    migrate_set_capability mapped-ram on
    migrate_set_capability postcopy-ram on

Use a ``file:`` URI for migration:

.. code-block:: text

    migrate_incoming file:/path/to/snapshot/file

Limitations
===========

 - Multifd
    Fast snapshot load is currently incompatible with ``multifd``
    capability. While ``mapped-ram`` allows for parallel disk I/O,
    coupling it with ``postcopy`` capability requires additional
    infrastructure.

 - Host OS support
    Because this feautre essentially depends on ``userfaultfd``
    to trap page faults, it is supported only on Linux hosts.

 - vhost-user
    Fast snapshot load does not currently support ``vhost-user``
    backends.
