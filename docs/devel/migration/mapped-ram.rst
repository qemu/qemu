Mapped-ram
==========

Mapped-ram is a new stream format for the RAM section designed to
supplement the existing ``file:`` migration and make it compatible
with ``multifd``. This enables parallel migration of a guest's RAM to
a file.

The core of the feature is to ensure that RAM pages are mapped
directly to offsets in the resulting migration file. This enables the
``multifd`` threads to write exclusively to those offsets even if the
guest is constantly dirtying pages (i.e. live migration). Another
benefit is that the resulting file will have a bounded size, since
pages which are dirtied multiple times will always go to a fixed
location in the file, rather than constantly being added to a
sequential stream. Having the pages at fixed offsets also allows the
usage of O_DIRECT for save/restore of the migration stream as the
pages are ensured to be written respecting O_DIRECT alignment
restrictions (direct-io support not yet implemented).

Usage
-----

On both source and destination, enable the ``multifd`` and
``mapped-ram`` capabilities:

    ``migrate_set_capability multifd on``

    ``migrate_set_capability mapped-ram on``

Use a ``file:`` URL for migration:

    ``migrate file:/path/to/migration/file``

Mapped-ram migration is best done non-live, i.e. by stopping the VM on
the source side before migrating.

Use-cases
---------

The mapped-ram feature was designed for use cases where the migration
stream will be directed to a file in the filesystem and not
immediately restored on the destination VM [#]_. These could be
thought of as snapshots. We can further categorize them into live and
non-live.

- Non-live snapshot

If the use case requires a VM to be stopped before taking a snapshot,
that's the ideal scenario for mapped-ram migration. Not having to
track dirty pages, the migration will write the RAM pages to the disk
as fast as it can.

Note: if a snapshot is taken of a running VM, but the VM will be
stopped after the snapshot by the admin, then consider stopping it
right before the snapshot to take benefit of the performance gains
mentioned above.

- Live snapshot

If the use case requires that the VM keeps running during and after
the snapshot operation, then mapped-ram migration can still be used,
but will be less performant. Other strategies such as
background-snapshot should be evaluated as well. One benefit of
mapped-ram in this scenario is portability since background-snapshot
depends on async dirty tracking (KVM_GET_DIRTY_LOG) which is not
supported outside of Linux.

.. [#] While this same effect could be obtained with the usage of
       snapshots or the ``file:`` migration alone, mapped-ram provides
       a performance increase for VMs with larger RAM sizes (10s to
       100s of GiBs), specially if the VM has been stopped beforehand.

RAM section format
------------------

Instead of having a sequential stream of pages that follow the
RAMBlock headers, the dirty pages for a RAMBlock follow its header
instead. This ensures that each RAM page has a fixed offset in the
resulting migration file.

A bitmap is introduced to track which pages have been written in the
migration file. Pages are written at a fixed location for every
ramblock. Zero pages are ignored as they'd be zero in the destination
migration as well.

::

 Without mapped-ram:                  With mapped-ram:

 ---------------------               --------------------------------
 | ramblock 1 header |               | ramblock 1 header            |
 ---------------------               --------------------------------
 | ramblock 2 header |               | ramblock 1 mapped-ram header |
 ---------------------               --------------------------------
 | ...               |               | padding to next 1MB boundary |
 ---------------------               | ...                          |
 | ramblock n header |               --------------------------------
 ---------------------               | ramblock 1 pages             |
 | RAM_SAVE_FLAG_EOS |               | ...                          |
 ---------------------               --------------------------------
 | stream of pages   |               | ramblock 2 header            |
 | (iter 1)          |               --------------------------------
 | ...               |               | ramblock 2 mapped-ram header |
 ---------------------               --------------------------------
 | RAM_SAVE_FLAG_EOS |               | padding to next 1MB boundary |
 ---------------------               | ...                          |
 | stream of pages   |               --------------------------------
 | (iter 2)          |               | ramblock 2 pages             |
 | ...               |               | ...                          |
 ---------------------               --------------------------------
 | ...               |               | ...                          |
 ---------------------               --------------------------------
                                     | RAM_SAVE_FLAG_EOS            |
                                     --------------------------------
                                     | ...                          |
                                     --------------------------------

where:
 - ramblock header: the generic information for a ramblock, such as
   idstr, used_len, etc.

 - ramblock mapped-ram header: the information added by this feature:
   bitmap of pages written, bitmap size and offset of pages in the
   migration file.

Restrictions
------------

Since pages are written to their relative offsets and out of order
(due to the memory dirtying patterns), streaming channels such as
sockets are not supported. A seekable channel such as a file is
required. This can be verified in the QIOChannel by the presence of
the QIO_CHANNEL_FEATURE_SEEKABLE.

The improvements brought by this feature apply only to guest physical
RAM. Other types of memory such as VRAM are migrated as part of device
states.
