..
   Copyright 2015 John Snow <jsnow@redhat.com> and Red Hat, Inc.
   All rights reserved.

   This file is licensed via The FreeBSD Documentation License, the full
   text of which is included at the end of this document.

====================================
Dirty Bitmaps and Incremental Backup
====================================

-  Dirty Bitmaps are objects that track which data needs to be backed up
   for the next incremental backup.

-  Dirty bitmaps can be created at any time and attached to any node
   (not just complete drives).

.. contents::

Dirty Bitmap Names
------------------

-  A dirty bitmap's name is unique to the node, but bitmaps attached to
   different nodes can share the same name.

-  Dirty bitmaps created for internal use by QEMU may be anonymous and
   have no name, but any user-created bitmaps must have a name. There
   can be any number of anonymous bitmaps per node.

-  The name of a user-created bitmap must not be empty ("").

Bitmap Modes
------------

-  A bitmap can be "frozen," which means that it is currently in-use by
   a backup operation and cannot be deleted, renamed, written to, reset,
   etc.

-  The normal operating mode for a bitmap is "active."

Basic QMP Usage
---------------

Supported Commands
~~~~~~~~~~~~~~~~~~

- ``block-dirty-bitmap-add``
- ``block-dirty-bitmap-remove``
- ``block-dirty-bitmap-clear``

Creation
~~~~~~~~

-  To create a new bitmap, enabled, on the drive with id=drive0:

.. code:: json

    { "execute": "block-dirty-bitmap-add",
      "arguments": {
        "node": "drive0",
        "name": "bitmap0"
      }
    }

-  This bitmap will have a default granularity that matches the cluster
   size of its associated drive, if available, clamped to between [4KiB,
   64KiB]. The current default for qcow2 is 64KiB.

-  To create a new bitmap that tracks changes in 32KiB segments:

.. code:: json

    { "execute": "block-dirty-bitmap-add",
      "arguments": {
        "node": "drive0",
        "name": "bitmap0",
        "granularity": 32768
      }
    }

Deletion
~~~~~~~~

-  Bitmaps that are frozen cannot be deleted.

-  Deleting the bitmap does not impact any other bitmaps attached to the
   same node, nor does it affect any backups already created from this
   node.

-  Because bitmaps are only unique to the node to which they are
   attached, you must specify the node/drive name here, too.

.. code:: json

    { "execute": "block-dirty-bitmap-remove",
      "arguments": {
        "node": "drive0",
        "name": "bitmap0"
      }
    }

Resetting
~~~~~~~~~

-  Resetting a bitmap will clear all information it holds.

-  An incremental backup created from an empty bitmap will copy no data,
   as if nothing has changed.

.. code:: json

    { "execute": "block-dirty-bitmap-clear",
      "arguments": {
        "node": "drive0",
        "name": "bitmap0"
      }
    }

Transactions
------------

Justification
~~~~~~~~~~~~~

Bitmaps can be safely modified when the VM is paused or halted by using
the basic QMP commands. For instance, you might perform the following
actions:

1. Boot the VM in a paused state.
2. Create a full drive backup of drive0.
3. Create a new bitmap attached to drive0.
4. Resume execution of the VM.
5. Incremental backups are ready to be created.

At this point, the bitmap and drive backup would be correctly in sync,
and incremental backups made from this point forward would be correctly
aligned to the full drive backup.

This is not particularly useful if we decide we want to start
incremental backups after the VM has been running for a while, for which
we will need to perform actions such as the following:

1. Boot the VM and begin execution.
2. Using a single transaction, perform the following operations:

   -  Create ``bitmap0``.
   -  Create a full drive backup of ``drive0``.

3. Incremental backups are now ready to be created.

Supported Bitmap Transactions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  ``block-dirty-bitmap-add``
-  ``block-dirty-bitmap-clear``

The usages are identical to their respective QMP commands, but see below
for examples.

Example: New Incremental Backup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As outlined in the justification, perhaps we want to create a new
incremental backup chain attached to a drive.

.. code:: json

    { "execute": "transaction",
      "arguments": {
        "actions": [
          {"type": "block-dirty-bitmap-add",
           "data": {"node": "drive0", "name": "bitmap0"} },
          {"type": "drive-backup",
           "data": {"device": "drive0", "target": "/path/to/full_backup.img",
                    "sync": "full", "format": "qcow2"} }
        ]
      }
    }

Example: New Incremental Backup Anchor Point
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Maybe we just want to create a new full backup with an existing bitmap
and want to reset the bitmap to track the new chain.

.. code:: json

    { "execute": "transaction",
      "arguments": {
        "actions": [
          {"type": "block-dirty-bitmap-clear",
           "data": {"node": "drive0", "name": "bitmap0"} },
          {"type": "drive-backup",
           "data": {"device": "drive0", "target": "/path/to/new_full_backup.img",
                    "sync": "full", "format": "qcow2"} }
        ]
      }
    }

Incremental Backups
-------------------

The star of the show.

**Nota Bene!** Only incremental backups of entire drives are supported
for now. So despite the fact that you can attach a bitmap to any
arbitrary node, they are only currently useful when attached to the root
node. This is because drive-backup only supports drives/devices instead
of arbitrary nodes.

Example: First Incremental Backup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Create a full backup and sync it to the dirty bitmap, as in the
   transactional examples above; or with the VM offline, manually create
   a full copy and then create a new bitmap before the VM begins
   execution.

   -  Let's assume the full backup is named ``full_backup.img``.
   -  Let's assume the bitmap you created is ``bitmap0`` attached to
      ``drive0``.

2. Create a destination image for the incremental backup that utilizes
   the full backup as a backing image.

   -  Let's assume the new incremental image is named
      ``incremental.0.img``.

   .. code:: bash

       $ qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2

3. Issue the incremental backup command:

   .. code:: json

       { "execute": "drive-backup",
         "arguments": {
           "device": "drive0",
           "bitmap": "bitmap0",
           "target": "incremental.0.img",
           "format": "qcow2",
           "sync": "incremental",
           "mode": "existing"
         }
       }

Example: Second Incremental Backup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Create a new destination image for the incremental backup that points
   to the previous one, e.g.: ``incremental.1.img``

   .. code:: bash

       $ qemu-img create -f qcow2 incremental.1.img -b incremental.0.img -F qcow2

2. Issue a new incremental backup command. The only difference here is
   that we have changed the target image below.

   .. code:: json

       { "execute": "drive-backup",
         "arguments": {
           "device": "drive0",
           "bitmap": "bitmap0",
           "target": "incremental.1.img",
           "format": "qcow2",
           "sync": "incremental",
           "mode": "existing"
         }
       }

Errors
------

-  In the event of an error that occurs after a backup job is
   successfully launched, either by a direct QMP command or a QMP
   transaction, the user will receive a ``BLOCK_JOB_COMPLETE`` event with
   a failure message, accompanied by a ``BLOCK_JOB_ERROR`` event.

-  In the case of an event being cancelled, the user will receive a
   ``BLOCK_JOB_CANCELLED`` event instead of a pair of COMPLETE and ERROR
   events.

-  In either case, the incremental backup data contained within the
   bitmap is safely rolled back, and the data within the bitmap is not
   lost. The image file created for the failed attempt can be safely
   deleted.

-  Once the underlying problem is fixed (e.g. more storage space is
   freed up), you can simply retry the incremental backup command with
   the same bitmap.

Example
~~~~~~~

1. Create a target image:

   .. code:: bash

       $ qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2

2. Attempt to create an incremental backup via QMP:

   .. code:: json

       { "execute": "drive-backup",
         "arguments": {
           "device": "drive0",
           "bitmap": "bitmap0",
           "target": "incremental.0.img",
           "format": "qcow2",
           "sync": "incremental",
           "mode": "existing"
         }
       }

3. Receive an event notifying us of failure:

   .. code:: json

       { "timestamp": { "seconds": 1424709442, "microseconds": 844524 },
         "data": { "speed": 0, "offset": 0, "len": 67108864,
                   "error": "No space left on device",
                   "device": "drive1", "type": "backup" },
         "event": "BLOCK_JOB_COMPLETED" }

4. Delete the failed incremental, and re-create the image.

   .. code:: bash

       $ rm incremental.0.img
       $ qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2

5. Retry the command after fixing the underlying problem, such as
   freeing up space on the backup volume:

   .. code:: json

       { "execute": "drive-backup",
         "arguments": {
           "device": "drive0",
           "bitmap": "bitmap0",
           "target": "incremental.0.img",
           "format": "qcow2",
           "sync": "incremental",
           "mode": "existing"
         }
       }

6. Receive confirmation that the job completed successfully:

   .. code:: json

       { "timestamp": { "seconds": 1424709668, "microseconds": 526525 },
         "data": { "device": "drive1", "type": "backup",
                   "speed": 0, "len": 67108864, "offset": 67108864},
         "event": "BLOCK_JOB_COMPLETED" }

Partial Transactional Failures
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  Sometimes, a transaction will succeed in launching and return
   success, but then later the backup jobs themselves may fail. It is
   possible that a management application may have to deal with a
   partial backup failure after a successful transaction.

-  If multiple backup jobs are specified in a single transaction, when
   one of them fails, it will not interact with the other backup jobs in
   any way.

-  The job(s) that succeeded will clear the dirty bitmap associated with
   the operation, but the job(s) that failed will not. It is not "safe"
   to delete any incremental backups that were created successfully in
   this scenario, even though others failed.

Example
^^^^^^^

-  QMP example highlighting two backup jobs:

   .. code:: json

       { "execute": "transaction",
         "arguments": {
           "actions": [
             { "type": "drive-backup",
               "data": { "device": "drive0", "bitmap": "bitmap0",
                         "format": "qcow2", "mode": "existing",
                         "sync": "incremental", "target": "d0-incr-1.qcow2" } },
             { "type": "drive-backup",
               "data": { "device": "drive1", "bitmap": "bitmap1",
                         "format": "qcow2", "mode": "existing",
                         "sync": "incremental", "target": "d1-incr-1.qcow2" } },
           ]
         }
       }

-  QMP example response, highlighting one success and one failure:

   -  Acknowledgement that the Transaction was accepted and jobs were
      launched:

      .. code:: json

          { "return": {} }

   -  Later, QEMU sends notice that the first job was completed:

      .. code:: json

          { "timestamp": { "seconds": 1447192343, "microseconds": 615698 },
            "data": { "device": "drive0", "type": "backup",
                       "speed": 0, "len": 67108864, "offset": 67108864 },
            "event": "BLOCK_JOB_COMPLETED"
          }

   -  Later yet, QEMU sends notice that the second job has failed:

      .. code:: json

          { "timestamp": { "seconds": 1447192399, "microseconds": 683015 },
            "data": { "device": "drive1", "action": "report",
                      "operation": "read" },
            "event": "BLOCK_JOB_ERROR" }

      .. code:: json

          { "timestamp": { "seconds": 1447192399, "microseconds":
          685853 }, "data": { "speed": 0, "offset": 0, "len": 67108864,
          "error": "Input/output error", "device": "drive1", "type":
          "backup" }, "event": "BLOCK_JOB_COMPLETED" }

-  In the above example, ``d0-incr-1.qcow2`` is valid and must be kept,
   but ``d1-incr-1.qcow2`` is invalid and should be deleted. If a VM-wide
   incremental backup of all drives at a point-in-time is to be made,
   new backups for both drives will need to be made, taking into account
   that a new incremental backup for drive0 needs to be based on top of
   ``d0-incr-1.qcow2``.

Grouped Completion Mode
~~~~~~~~~~~~~~~~~~~~~~~

-  While jobs launched by transactions normally complete or fail on
   their own, it is possible to instruct them to complete or fail
   together as a group.

-  QMP transactions take an optional properties structure that can
   affect the semantics of the transaction.

-  The "completion-mode" transaction property can be either "individual"
   which is the default, legacy behavior described above, or "grouped,"
   a new behavior detailed below.

-  Delayed Completion: In grouped completion mode, no jobs will report
   success until all jobs are ready to report success.

-  Grouped failure: If any job fails in grouped completion mode, all
   remaining jobs will be cancelled. Any incremental backups will
   restore their dirty bitmap objects as if no backup command was ever
   issued.

   -  Regardless of if QEMU reports a particular incremental backup job
      as CANCELLED or as an ERROR, the in-memory bitmap will be
      restored.

Example
^^^^^^^

-  Here's the same example scenario from above with the new property:

   .. code:: json

       { "execute": "transaction",
         "arguments": {
           "actions": [
             { "type": "drive-backup",
               "data": { "device": "drive0", "bitmap": "bitmap0",
                         "format": "qcow2", "mode": "existing",
                         "sync": "incremental", "target": "d0-incr-1.qcow2" } },
             { "type": "drive-backup",
               "data": { "device": "drive1", "bitmap": "bitmap1",
                         "format": "qcow2", "mode": "existing",
                         "sync": "incremental", "target": "d1-incr-1.qcow2" } },
           ],
           "properties": {
             "completion-mode": "grouped"
           }
         }
       }

-  QMP example response, highlighting a failure for ``drive2``:

   -  Acknowledgement that the Transaction was accepted and jobs were
      launched:

      .. code:: json

          { "return": {} }

   -  Later, QEMU sends notice that the second job has errored out, but
      that the first job was also cancelled:

      .. code:: json

          { "timestamp": { "seconds": 1447193702, "microseconds": 632377 },
            "data": { "device": "drive1", "action": "report",
                      "operation": "read" },
            "event": "BLOCK_JOB_ERROR" }

      .. code:: json

          { "timestamp": { "seconds": 1447193702, "microseconds": 640074 },
            "data": { "speed": 0, "offset": 0, "len": 67108864,
                      "error": "Input/output error",
                      "device": "drive1", "type": "backup" },
            "event": "BLOCK_JOB_COMPLETED" }

      .. code:: json

          { "timestamp": { "seconds": 1447193702, "microseconds": 640163 },
            "data": { "device": "drive0", "type": "backup", "speed": 0,
                      "len": 67108864, "offset": 16777216 },
            "event": "BLOCK_JOB_CANCELLED" }

.. raw:: html

   <!--
   The FreeBSD Documentation License

   Redistribution and use in source (Markdown) and 'compiled' forms (SGML, HTML,
   PDF, PostScript, RTF and so forth) with or without modification, are permitted
   provided that the following conditions are met:

   Redistributions of source code (Markdown) must retain the above copyright
   notice, this list of conditions and the following disclaimer of this file
   unmodified.

   Redistributions in compiled form (transformed to other DTDs, converted to PDF,
   PostScript, RTF and other formats) must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation and/or
   other materials provided with the distribution.

   THIS DOCUMENTATION IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR  PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS  BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
   THIS DOCUMENTATION, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
   -->
