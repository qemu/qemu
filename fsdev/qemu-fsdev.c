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
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-fsdev.h"
#include "qemu/queue.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"

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
    const char **opts;
} FsDriverTable;

typedef struct FsDriverListEntry {
    FsDriverEntry fse;
    QTAILQ_ENTRY(FsDriverListEntry) next;
} FsDriverListEntry;

static QTAILQ_HEAD(, FsDriverListEntry) fsdriver_entries =
    QTAILQ_HEAD_INITIALIZER(fsdriver_entries);

#define COMMON_FS_DRIVER_OPTIONS "id", "fsdriver", "readonly"

static FsDriverTable FsDrivers[] = {
    {
        .name = "local",
        .ops = &local_ops,
        .opts = (const char * []) {
            COMMON_FS_DRIVER_OPTIONS,
            "security_model",
            "path",
            "writeout",
            "fmode",
            "dmode",
            "multidevs",
            "throttling.bps-total",
            "throttling.bps-read",
            "throttling.bps-write",
            "throttling.iops-total",
            "throttling.iops-read",
            "throttling.iops-write",
            "throttling.bps-total-max",
            "throttling.bps-read-max",
            "throttling.bps-write-max",
            "throttling.iops-total-max",
            "throttling.iops-read-max",
            "throttling.iops-write-max",
            "throttling.bps-total-max-length",
            "throttling.bps-read-max-length",
            "throttling.bps-write-max-length",
            "throttling.iops-total-max-length",
            "throttling.iops-read-max-length",
            "throttling.iops-write-max-length",
            "throttling.iops-size",
        },
    },
    {
        .name = "synth",
        .ops = &synth_ops,
        .opts = (const char * []) {
            COMMON_FS_DRIVER_OPTIONS,
        },
    },
    {
        .name = "proxy",
        .ops = &proxy_ops,
        .opts = (const char * []) {
            COMMON_FS_DRIVER_OPTIONS,
            "socket",
            "sock_fd",
            "writeout",
        },
    },
};

static int validate_opt(void *opaque, const char *name, const char *value,
                        Error **errp)
{
    FsDriverTable *drv = opaque;
    const char **opt;

    for (opt = drv->opts; *opt; opt++) {
        if (!strcmp(*opt, name)) {
            return 0;
        }
    }

    error_setg(errp, "'%s' is invalid for fsdriver '%s'", name, drv->name);
    return -1;
}

int qemu_fsdev_add(QemuOpts *opts, Error **errp)
{
    int i;
    struct FsDriverListEntry *fsle;
    const char *fsdev_id = qemu_opts_id(opts);
    const char *fsdriver = qemu_opt_get(opts, "fsdriver");
    const char *writeout = qemu_opt_get(opts, "writeout");
    bool ro = qemu_opt_get_bool(opts, "readonly", 0);

    if (!fsdev_id) {
        error_setg(errp, "fsdev: No id specified");
        return -1;
    }

    if (fsdriver) {
        for (i = 0; i < ARRAY_SIZE(FsDrivers); i++) {
            if (strcmp(FsDrivers[i].name, fsdriver) == 0) {
                break;
            }
        }

        if (i == ARRAY_SIZE(FsDrivers)) {
            error_setg(errp, "fsdev: fsdriver %s not found", fsdriver);
            return -1;
        }
    } else {
        error_setg(errp, "fsdev: No fsdriver specified");
        return -1;
    }

    if (qemu_opt_foreach(opts, validate_opt, &FsDrivers[i], errp)) {
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
        if (fsle->fse.ops->parse_opts(opts, &fsle->fse, errp)) {
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
