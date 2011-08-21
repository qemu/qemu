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
#include "qemu-config.h"

static QTAILQ_HEAD(FsTypeEntry_head, FsTypeListEntry) fstype_entries =
    QTAILQ_HEAD_INITIALIZER(fstype_entries);

static FsTypeTable FsTypes[] = {
    { .name = "local", .ops = &local_ops},
};

int qemu_fsdev_add(QemuOpts *opts)
{
    struct FsTypeListEntry *fsle;
    int i;
    const char *fsdev_id = qemu_opts_id(opts);
    const char *fstype = qemu_opt_get(opts, "fstype");
    const char *path = qemu_opt_get(opts, "path");
    const char *sec_model = qemu_opt_get(opts, "security_model");

    if (!fsdev_id) {
        fprintf(stderr, "fsdev: No id specified\n");
        return -1;
    }

    if (fstype) {
        for (i = 0; i < ARRAY_SIZE(FsTypes); i++) {
            if (strcmp(FsTypes[i].name, fstype) == 0) {
                break;
            }
        }

        if (i == ARRAY_SIZE(FsTypes)) {
            fprintf(stderr, "fsdev: fstype %s not found\n", fstype);
            return -1;
        }
    } else {
        fprintf(stderr, "fsdev: No fstype specified\n");
        return -1;
    }

    if (!sec_model) {
        fprintf(stderr, "fsdev: No security_model specified.\n");
        return -1;
    }

    if (!path) {
        fprintf(stderr, "fsdev: No path specified.\n");
        return -1;
    }

    fsle = g_malloc(sizeof(*fsle));

    fsle->fse.fsdev_id = g_strdup(fsdev_id);
    fsle->fse.path = g_strdup(path);
    fsle->fse.security_model = g_strdup(sec_model);
    fsle->fse.ops = FsTypes[i].ops;

    QTAILQ_INSERT_TAIL(&fstype_entries, fsle, next);
    return 0;

}

FsTypeEntry *get_fsdev_fsentry(char *id)
{
    if (id) {
        struct FsTypeListEntry *fsle;

        QTAILQ_FOREACH(fsle, &fstype_entries, next) {
            if (strcmp(fsle->fse.fsdev_id, id) == 0) {
                return &fsle->fse;
            }
        }
    }
    return NULL;
}

static void fsdev_register_config(void)
{
    qemu_add_opts(&qemu_fsdev_opts);
    qemu_add_opts(&qemu_virtfs_opts);
}
machine_init(fsdev_register_config);

