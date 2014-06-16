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

#include "hw/hw.h"
#include "hw/boards.h"
#include "sysemu/blockdev.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"

DriveInfo *add_init_drive(const char *optstr)
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

void drive_hot_add(Monitor *mon, const QDict *qdict)
{
    DriveInfo *dinfo = NULL;
    const char *opts = qdict_get_str(qdict, "opts");

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
        if (pci_drive_hot_add(mon, qdict, dinfo)) {
            goto err;
        }
    }
    return;

err:
    if (dinfo) {
        drive_del(dinfo);
    }
}
