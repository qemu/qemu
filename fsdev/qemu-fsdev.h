/*
 * Virtio 9p
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Gautham R Shenoy <ego@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_FSDEV_H
#define QEMU_FSDEV_H
#include "qemu-option.h"
#include "file-op-9p.h"


/*
 * A table to store the various file systems and their callback operations.
 * -----------------
 * fstype | ops
 * -----------------
 *  local | local_ops
 *  .     |
 *  .     |
 *  .     |
 *  .     |
 * -----------------
 *  etc
 */
typedef struct FsDriverTable {
    const char *name;
    FileOperations *ops;
} FsDriverTable;

/*
 * Structure to store the various fsdev's passed through command line.
 */
typedef struct FsDriverEntry {
    char *fsdev_id;
    char *path;
    int export_flags;
    FileOperations *ops;
} FsDriverEntry;

typedef struct FsDriverListEntry {
    FsDriverEntry fse;
    QTAILQ_ENTRY(FsDriverListEntry) next;
} FsDriverListEntry;

int qemu_fsdev_add(QemuOpts *opts);
FsDriverEntry *get_fsdev_fsentry(char *id);
extern FileOperations local_ops;
extern FileOperations handle_ops;
extern FileOperations synth_ops;
#endif
