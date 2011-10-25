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

static QTAILQ_HEAD(FsDriverEntry_head, FsDriverListEntry) fsdriver_entries =
    QTAILQ_HEAD_INITIALIZER(fsdriver_entries);

static FsDriverTable FsDrivers[] = {
    { .name = "local", .ops = &local_ops},
    { .name = "handle", .ops = &handle_ops},
    { .name = "synth", .ops = &synth_ops},
};

int qemu_fsdev_add(QemuOpts *opts)
{
    struct FsDriverListEntry *fsle;
    int i;
    const char *fsdev_id = qemu_opts_id(opts);
    const char *fsdriver = qemu_opt_get(opts, "fsdriver");
    const char *path = qemu_opt_get(opts, "path");
    const char *sec_model = qemu_opt_get(opts, "security_model");
    const char *writeout = qemu_opt_get(opts, "writeout");
    bool ro = qemu_opt_get_bool(opts, "readonly", 0);

    if (!fsdev_id) {
        fprintf(stderr, "fsdev: No id specified\n");
        return -1;
    }

    if (fsdriver) {
        for (i = 0; i < ARRAY_SIZE(FsDrivers); i++) {
            if (strcmp(FsDrivers[i].name, fsdriver) == 0) {
                break;
            }
        }

        if (i == ARRAY_SIZE(FsDrivers)) {
            fprintf(stderr, "fsdev: fsdriver %s not found\n", fsdriver);
            return -1;
        }
    } else {
        fprintf(stderr, "fsdev: No fsdriver specified\n");
        return -1;
    }

    if (!strcmp(fsdriver, "local") && !sec_model) {
        fprintf(stderr, "security model not specified, "
                "local fs needs security model\nvalid options are:"
                "\tsecurity_model=[passthrough|mapped|none]\n");
        return -1;
    }

    if (strcmp(fsdriver, "local") && sec_model) {
        fprintf(stderr, "only local fs driver needs security model\n");
        return -1;
    }

    if (!path) {
        fprintf(stderr, "fsdev: No path specified.\n");
        return -1;
    }

    fsle = g_malloc(sizeof(*fsle));

    fsle->fse.fsdev_id = g_strdup(fsdev_id);
    fsle->fse.path = g_strdup(path);
    fsle->fse.ops = FsDrivers[i].ops;
    fsle->fse.export_flags = 0;
    if (writeout) {
        if (!strcmp(writeout, "immediate")) {
            fsle->fse.export_flags |= V9FS_IMMEDIATE_WRITEOUT;
        }
    }
    if (ro) {
        fsle->fse.export_flags |= V9FS_RDONLY;
    } else {
        fsle->fse.export_flags &= ~V9FS_RDONLY;
    }

    if (strcmp(fsdriver, "local")) {
        goto done;
    }

    if (!strcmp(sec_model, "passthrough")) {
        fsle->fse.export_flags |= V9FS_SM_PASSTHROUGH;
    } else if (!strcmp(sec_model, "mapped")) {
        fsle->fse.export_flags |= V9FS_SM_MAPPED;
    } else if (!strcmp(sec_model, "none")) {
        fsle->fse.export_flags |= V9FS_SM_NONE;
    } else {
        fprintf(stderr, "Invalid security model %s specified, valid options are"
                "\n\t [passthrough|mapped|none]\n", sec_model);
        return -1;
    }
done:
    QTAILQ_INSERT_TAIL(&fsdriver_entries, fsle, next);
    return 0;
}

FsDriverEntry *get_fsdev_fsentry(char *id)
{
    if (id) {
        struct FsDriverListEntry *fsle;

        QTAILQ_FOREACH(fsle, &fsdriver_entries, next) {
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

