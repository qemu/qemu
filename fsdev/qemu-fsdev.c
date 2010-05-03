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
#include <stdio.h>
#include <string.h>
#include "qemu-fsdev.h"
#include "qemu-queue.h"
#include "osdep.h"
#include "qemu-common.h"

static QTAILQ_HEAD(FsTypeEntry_head, FsTypeListEntry) fstype_entries =
    QTAILQ_HEAD_INITIALIZER(fstype_entries);

static FsTypeTable FsTypes[] = {
    { .name = "local", .ops = &local_ops},
};

int qemu_fsdev_add(QemuOpts *opts)
{
    struct FsTypeListEntry *fsle;
    int i;

    if (qemu_opts_id(opts) == NULL) {
        fprintf(stderr, "fsdev: No id specified\n");
        return -1;
    }

     for (i = 0; i < ARRAY_SIZE(FsTypes); i++) {
        if (strcmp(FsTypes[i].name, qemu_opt_get(opts, "fstype")) == 0) {
            break;
        }
    }

    if (i == ARRAY_SIZE(FsTypes)) {
        fprintf(stderr, "fsdev: fstype %s not found\n",
                    qemu_opt_get(opts, "fstype"));
        return -1;
    }

    fsle = qemu_malloc(sizeof(*fsle));

    fsle->fse.fsdev_id = qemu_strdup(qemu_opts_id(opts));
    fsle->fse.path = qemu_strdup(qemu_opt_get(opts, "path"));
    fsle->fse.ops = FsTypes[i].ops;

    QTAILQ_INSERT_TAIL(&fstype_entries, fsle, next);
    return 0;

}

FsTypeEntry *get_fsdev_fsentry(char *id)
{
    struct FsTypeListEntry *fsle;

    QTAILQ_FOREACH(fsle, &fstype_entries, next) {
        if (strcmp(fsle->fse.fsdev_id, id) == 0) {
            return &fsle->fse;
        }
    }
    return NULL;
}
