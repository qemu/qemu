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
#include "hw/file-op-9p.h"


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
typedef struct FsTypeTable {
    const char *name;
    FileOperations *ops;
} FsTypeTable;

/*
 * Structure to store the various fsdev's passed through command line.
 */
typedef struct FsTypeEntry {
    char *fsdev_id;
    char *path;
    char *security_model;
    FileOperations *ops;
} FsTypeEntry;

typedef struct FsTypeListEntry {
    FsTypeEntry fse;
    QTAILQ_ENTRY(FsTypeListEntry) next;
} FsTypeListEntry;

int qemu_fsdev_add(QemuOpts *opts);
FsTypeEntry *get_fsdev_fsentry(char *id);
extern FileOperations local_ops;
#endif
