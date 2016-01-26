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
#include "qemu/osdep.h"
#include "qemu-fsdev.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"

static QTAILQ_HEAD(FsDriverEntry_head, FsDriverListEntry) fsdriver_entries =
    QTAILQ_HEAD_INITIALIZER(fsdriver_entries);

static FsDriverTable FsDrivers[] = {
    { .name = "local", .ops = &local_ops},
#ifdef CONFIG_OPEN_BY_HANDLE
    { .name = "handle", .ops = &handle_ops},
#endif
    { .name = "synth", .ops = &synth_ops},
    { .name = "proxy", .ops = &proxy_ops},
};

int qemu_fsdev_add(QemuOpts *opts)
{
    int i;
    struct FsDriverListEntry *fsle;
    const char *fsdev_id = qemu_opts_id(opts);
    const char *fsdriver = qemu_opt_get(opts, "fsdriver");
    const char *writeout = qemu_opt_get(opts, "writeout");
    bool ro = qemu_opt_get_bool(opts, "readonly", 0);

    if (!fsdev_id) {
        error_report("fsdev: No id specified");
        return -1;
    }

    if (fsdriver) {
        for (i = 0; i < ARRAY_SIZE(FsDrivers); i++) {
            if (strcmp(FsDrivers[i].name, fsdriver) == 0) {
                break;
            }
        }

        if (i == ARRAY_SIZE(FsDrivers)) {
            error_report("fsdev: fsdriver %s not found", fsdriver);
            return -1;
        }
    } else {
        error_report("fsdev: No fsdriver specified");
        return -1;
    }

    fsle = g_malloc0(sizeof(*fsle));
    fsle->fse.fsdev_id = g_strdup(fsdev_id);
    fsle->fse.ops = FsDrivers[i].ops;
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

    if (fsle->fse.ops->parse_opts) {
        if (fsle->fse.ops->parse_opts(opts, &fsle->fse)) {
            g_free(fsle->fse.fsdev_id);
            g_free(fsle);
            return -1;
        }
    }

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
