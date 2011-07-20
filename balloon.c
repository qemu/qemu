/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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

#include "monitor.h"
#include "qjson.h"
#include "qint.h"
#include "cpu-common.h"
#include "kvm.h"
#include "balloon.h"
#include "trace.h"


static QEMUBalloonEvent *balloon_event_fn;
static QEMUBalloonStatus *balloon_stat_fn;
static void *balloon_opaque;

void qemu_add_balloon_handler(QEMUBalloonEvent *event_func,
                              QEMUBalloonStatus *stat_func, void *opaque)
{
    balloon_event_fn = event_func;
    balloon_stat_fn = stat_func;
    balloon_opaque = opaque;
}

static int qemu_balloon(ram_addr_t target)
{
    if (!balloon_event_fn) {
        return 0;
    }
    trace_balloon_event(balloon_opaque, target);
    balloon_event_fn(balloon_opaque, target);
    return 1;
}

static int qemu_balloon_status(MonitorCompletion cb, void *opaque)
{
    if (!balloon_stat_fn) {
        return 0;
    }
    balloon_stat_fn(balloon_opaque, cb, opaque);
    return 1;
}

static void print_balloon_stat(const char *key, QObject *obj, void *opaque)
{
    Monitor *mon = opaque;

    if (strcmp(key, "actual")) {
        monitor_printf(mon, ",%s=%" PRId64, key,
                       qint_get_int(qobject_to_qint(obj)));
    }
}

void monitor_print_balloon(Monitor *mon, const QObject *data)
{
    QDict *qdict;

    qdict = qobject_to_qdict(data);
    if (!qdict_haskey(qdict, "actual")) {
        return;
    }
    monitor_printf(mon, "balloon: actual=%" PRId64,
                   qdict_get_int(qdict, "actual") >> 20);
    qdict_iter(qdict, print_balloon_stat, mon);
    monitor_printf(mon, "\n");
}

/**
 * do_info_balloon(): Balloon information
 *
 * Make an asynchronous request for balloon info.  When the request completes
 * a QDict will be returned according to the following specification:
 *
 * - "actual": current balloon value in bytes
 * The following fields may or may not be present:
 * - "mem_swapped_in": Amount of memory swapped in (bytes)
 * - "mem_swapped_out": Amount of memory swapped out (bytes)
 * - "major_page_faults": Number of major faults
 * - "minor_page_faults": Number of minor faults
 * - "free_mem": Total amount of free and unused memory (bytes)
 * - "total_mem": Total amount of available memory (bytes)
 *
 * Example:
 *
 * { "actual": 1073741824, "mem_swapped_in": 0, "mem_swapped_out": 0,
 *   "major_page_faults": 142, "minor_page_faults": 239245,
 *   "free_mem": 1014185984, "total_mem": 1044668416 }
 */
int do_info_balloon(Monitor *mon, MonitorCompletion cb, void *opaque)
{
    int ret;

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        qerror_report(QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
        return -1;
    }

    ret = qemu_balloon_status(cb, opaque);
    if (!ret) {
        qerror_report(QERR_DEVICE_NOT_ACTIVE, "balloon");
        return -1;
    }

    return 0;
}

/**
 * do_balloon(): Request VM to change its memory allocation
 */
int do_balloon(Monitor *mon, const QDict *params,
	       MonitorCompletion cb, void *opaque)
{
    int ret;

    if (kvm_enabled() && !kvm_has_sync_mmu()) {
        qerror_report(QERR_KVM_MISSING_CAP, "synchronous MMU", "balloon");
        return -1;
    }

    ret = qemu_balloon(qdict_get_int(params, "value"));
    if (ret == 0) {
        qerror_report(QERR_DEVICE_NOT_ACTIVE, "balloon");
        return -1;
    }

    cb(opaque, NULL);
    return 0;
}
