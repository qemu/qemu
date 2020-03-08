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
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "qapi/qmp/qdict.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "block/block_int.h"


void hmp_drive_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    DriveInfo *dinfo;
    QemuOpts *opts;
    MachineClass *mc;
    const char *optstr = qdict_get_str(qdict, "opts");
    bool node = qdict_get_try_bool(qdict, "node", false);

    if (node) {
        hmp_drive_add_node(mon, optstr);
        return;
    }

    opts = drive_def(optstr);
    if (!opts)
        return;

    mc = MACHINE_GET_CLASS(current_machine);
    dinfo = drive_new(opts, mc->block_default_type, &err);
    if (err) {
        error_report_err(err);
        qemu_opts_del(opts);
        goto err;
    }

    if (!dinfo) {
        return;
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
