/*
 * QEMU device hotplug helpers
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "block/block_int.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"


static DriveInfo *add_init_drive(const char *optstr)
{
    DriveInfo *dinfo;
    QemuOpts *opts;
    MachineClass *mc;

    opts = drive_def(optstr);
    if (!opts)
        return NULL;

    mc = MACHINE_GET_CLASS(current_machine);
    dinfo = drive_new(opts, mc->block_default_type);
    if (!dinfo) {
        qemu_opts_del(opts);
        return NULL;
    }

    return dinfo;
}

void hmp_drive_add(Monitor *mon, const QDict *qdict)
{
    DriveInfo *dinfo = NULL;
    const char *opts = qdict_get_str(qdict, "opts");
    bool node = qdict_get_try_bool(qdict, "node", false);

    if (node) {
        hmp_drive_add_node(mon, opts);
        return;
    }

    dinfo = add_init_drive(opts);
    if (!dinfo) {
        goto err;
    }
    if (dinfo->devaddr) {
        monitor_printf(mon, "Parameter addr not supported\n");
        goto err;
    }

    switch (dinfo->type) {
    case IF_NONE:
        monitor_printf(mon, "OK\n");
        break;
    default:
        monitor_printf(mon, "Can't hot-add drive to type %d\n", dinfo->type);
        goto err;
    }
    return;

err:
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

static void check_parm(const char *key, QObject *obj, void *opaque)
{
    static const char *unwanted_keys[] = {
        "bus", "unit", "index", "if", "boot", "addr",
        NULL

    };
    int *stopped = opaque;
    const char **p;

    if (*stopped) {
        return;
    }

    for (p = unwanted_keys; *p; p++) {
        if (!strcmp(key, *p)) {
            error_report(QERR_INVALID_PARAMETER, key);
            *stopped = 1;
            return;
        }
    }
}

void qmp_simple_drive_add(QDict *qdict, QObject **ret_data, Error **errp)
{
    int stopped;
    Error *local_err = NULL;
    QemuOpts *opts;
    DriveInfo *dinfo;
    MachineClass *mc;

    if (!qdict_haskey(qdict, "id")) {
        error_setg(errp, QERR_MISSING_PARAMETER, "id");
        return;
    }

    stopped = 0;
    qdict_iter(qdict, check_parm, &stopped);
    if (stopped) {
        return;
    }

    opts = qemu_opts_from_qdict(&qemu_drive_opts, qdict, &local_err);
    if (!opts) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_opt_set(opts, "if", "none", &error_abort);
    mc = MACHINE_GET_CLASS(current_machine);
    dinfo = drive_new(opts, mc->block_default_type);
    if (!dinfo) {
        error_setg(errp, QERR_DEVICE_INIT_FAILED, qemu_opts_id(opts));
        qemu_opts_del(opts);
        return;
    }

    return;
}

void hmp_simple_drive_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_simple_drive_add((QDict *)qdict, NULL, &err);
    if (err) {
        error_report_err(err);
    }
}
