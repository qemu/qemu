/*
 * 9p
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
#include "file-op-9p.h"

int qemu_fsdev_add(QemuOpts *opts, Error **errp);
FsDriverEntry *get_fsdev_fsentry(char *id);
extern FileOperations local_ops;
extern FileOperations synth_ops;
extern FileOperations proxy_ops;
#endif
