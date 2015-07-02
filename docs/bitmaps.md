<!--
Copyright 2015 John Snow <jsnow@redhat.com> and Red Hat, Inc.
All rights reserved.

This file is licensed via The FreeBSD Documentation License, the full text of
which is included at the end of this document.
-->

# Dirty Bitmaps and Incremental Backup

* Dirty Bitmaps are objects that track which data needs to be backed up for the
  next incremental backup.

* Dirty bitmaps can be created at any time and attached to any node
  (not just complete drives.)

## Dirty Bitmap Names

* A dirty bitmap's name is unique to the node, but bitmaps attached to different
  nodes can share the same name.

## Bitmap Modes

* A Bitmap can be "frozen," which means that it is currently in-use by a backup
  operation and cannot be deleted, renamed, written to, reset,
  etc.

## Basic QMP Usage

### Supported Commands ###

* block-dirty-bitmap-add
* block-dirty-bitmap-remove
* block-dirty-bitmap-clear

### Creation

* To create a new bitmap, enabled, on the drive with id=drive0:

```json
{ "execute": "block-dirty-bitmap-add",
  "arguments": {
    "node": "drive0",
    "name": "bitmap0"
  }
}
```

* This bitmap will have a default granularity that matches the cluster size of
  its associated drive, if available, clamped to between [4KiB, 64KiB].
  The current default for qcow2 is 64KiB.

* To create a new bitmap that tracks changes in 32KiB segments:

```json
{ "execute": "block-dirty-bitmap-add",
  "arguments": {
    "node": "drive0",
    "name": "bitmap0",
    "granularity": 32768
  }
}
```

### Deletion

* Bitmaps that are frozen cannot be deleted.

* Deleting the bitmap does not impact any other bitmaps attached to the same
  node, nor does it affect any backups already created from this node.

* Because bitmaps are only unique to the node to which they are attached,
  you must specify the node/drive name here, too.

```json
{ "execute": "block-dirty-bitmap-remove",
  "arguments": {
    "node": "drive0",
    "name": "bitmap0"
  }
}
```

### Resetting

* Resetting a bitmap will clear all information it holds.

* An incremental backup created from an empty bitmap will copy no data,
  as if nothing has changed.

```json
{ "execute": "block-dirty-bitmap-clear",
  "arguments": {
    "node": "drive0",
    "name": "bitmap0"
  }
}
```

## Transactions (Not yet implemented)

* Transactional commands are forthcoming in a future version,
  and are not yet available for use. This section serves as
  documentation of intent for their design and usage.

### Justification

Bitmaps can be safely modified when the VM is paused or halted by using
the basic QMP commands. For instance, you might perform the following actions:

1. Boot the VM in a paused state.
2. Create a full drive backup of drive0.
3. Create a new bitmap attached to drive0.
4. Resume execution of the VM.
5. Incremental backups are ready to be created.

At this point, the bitmap and drive backup would be correctly in sync,
and incremental backups made from this point forward would be correctly aligned
to the full drive backup.

This is not particularly useful if we decide we want to start incremental
backups after the VM has been running for a while, for which we will need to
perform actions such as the following:

1. Boot the VM and begin execution.
2. Using a single transaction, perform the following operations:
    * Create bitmap0.
    * Create a full drive backup of drive0.
3. Incremental backups are now ready to be created.

### Supported Bitmap Transactions

* block-dirty-bitmap-add
* block-dirty-bitmap-clear

The usages are identical to their respective QMP commands, but see below
for examples.

### Example: New Incremental Backup

As outlined in the justification, perhaps we want to create a new incremental
backup chain attached to a drive.

```json
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
```

### Example: New Incremental Backup Anchor Point

Maybe we just want to create a new full backup with an existing bitmap and
want to reset the bitmap to track the new chain.

```json
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
```

## Incremental Backups

The star of the show.

**Nota Bene!** Only incremental backups of entire drives are supported for now.
So despite the fact that you can attach a bitmap to any arbitrary node, they are
only currently useful when attached to the root node. This is because
drive-backup only supports drives/devices instead of arbitrary nodes.

### Example: First Incremental Backup

1. Create a full backup and sync it to the dirty bitmap, as in the transactional
examples above; or with the VM offline, manually create a full copy and then
create a new bitmap before the VM begins execution.

    * Let's assume the full backup is named 'full_backup.img'.
    * Let's assume the bitmap you created is 'bitmap0' attached to 'drive0'.

2. Create a destination image for the incremental backup that utilizes the
full backup as a backing image.

    * Let's assume it is named 'incremental.0.img'.

    ```sh
    # qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2
    ```

3. Issue the incremental backup command:

    ```json
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
    ```

### Example: Second Incremental Backup

1. Create a new destination image for the incremental backup that points to the
   previous one, e.g.: 'incremental.1.img'

    ```sh
    # qemu-img create -f qcow2 incremental.1.img -b incremental.0.img -F qcow2
    ```

2. Issue a new incremental backup command. The only difference here is that we
   have changed the target image below.

    ```json
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
    ```

## Errors

* In the event of an error that occurs after a backup job is successfully
  launched, either by a direct QMP command or a QMP transaction, the user
  will receive a BLOCK_JOB_COMPLETE event with a failure message, accompanied
  by a BLOCK_JOB_ERROR event.

* In the case of an event being cancelled, the user will receive a
  BLOCK_JOB_CANCELLED event instead of a pair of COMPLETE and ERROR events.

* In either case, the incremental backup data contained within the bitmap is
  safely rolled back, and the data within the bitmap is not lost. The image
  file created for the failed attempt can be safely deleted.

* Once the underlying problem is fixed (e.g. more storage space is freed up),
  you can simply retry the incremental backup command with the same bitmap.

### Example

1. Create a target image:

    ```sh
    # qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2
    ```

2. Attempt to create an incremental backup via QMP:

    ```json
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
    ```

3. Receive an event notifying us of failure:

    ```json
    { "timestamp": { "seconds": 1424709442, "microseconds": 844524 },
      "data": { "speed": 0, "offset": 0, "len": 67108864,
                "error": "No space left on device",
                "device": "drive1", "type": "backup" },
      "event": "BLOCK_JOB_COMPLETED" }
    ```

4. Delete the failed incremental, and re-create the image.

    ```sh
    # rm incremental.0.img
    # qemu-img create -f qcow2 incremental.0.img -b full_backup.img -F qcow2
    ```

5. Retry the command after fixing the underlying problem,
   such as freeing up space on the backup volume:

    ```json
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
    ```

6. Receive confirmation that the job completed successfully:

    ```json
    { "timestamp": { "seconds": 1424709668, "microseconds": 526525 },
      "data": { "device": "drive1", "type": "backup",
                "speed": 0, "len": 67108864, "offset": 67108864},
      "event": "BLOCK_JOB_COMPLETED" }
    ```

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
