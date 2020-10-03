/*
 * replay-debugging.c
 *
 * Copyright (c) 2010-2020 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "replay-internal.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-replay.h"
#include "qapi/qmp/qdict.h"
#include "qemu/timer.h"

void hmp_info_replay(Monitor *mon, const QDict *qdict)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        monitor_printf(mon, "Record/replay is not active\n");
    } else {
        monitor_printf(mon,
            "%s execution '%s': instruction count = %"PRId64"\n",
            replay_mode == REPLAY_MODE_RECORD ? "Recording" : "Replaying",
            replay_get_filename(), replay_get_current_icount());
    }
}

ReplayInfo *qmp_query_replay(Error **errp)
{
    ReplayInfo *retval = g_new0(ReplayInfo, 1);

    retval->mode = replay_mode;
    if (replay_get_filename()) {
        retval->filename = g_strdup(replay_get_filename());
        retval->has_filename = true;
    }
    retval->icount = replay_get_current_icount();
    return retval;
}

static void replay_break(uint64_t icount, QEMUTimerCB callback, void *opaque)
{
    assert(replay_mode == REPLAY_MODE_PLAY);
    assert(replay_mutex_locked());
    assert(replay_break_icount >= replay_get_current_icount());
    assert(callback);

    replay_break_icount = icount;

    if (replay_break_timer) {
        timer_del(replay_break_timer);
    }
    replay_break_timer = timer_new_ns(QEMU_CLOCK_REALTIME,
                                      callback, opaque);
}

static void replay_delete_break(void)
{
    assert(replay_mode == REPLAY_MODE_PLAY);
    assert(replay_mutex_locked());

    if (replay_break_timer) {
        timer_del(replay_break_timer);
        timer_free(replay_break_timer);
        replay_break_timer = NULL;
    }
    replay_break_icount = -1ULL;
}

static void replay_stop_vm(void *opaque)
{
    vm_stop(RUN_STATE_PAUSED);
    replay_delete_break();
}

void qmp_replay_break(int64_t icount, Error **errp)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        if (icount >= replay_get_current_icount()) {
            replay_break(icount, replay_stop_vm, NULL);
        } else {
            error_setg(errp,
                "cannot set breakpoint at the instruction in the past");
        }
    } else {
        error_setg(errp, "setting the breakpoint is allowed only in play mode");
    }
}

void hmp_replay_break(Monitor *mon, const QDict *qdict)
{
    int64_t icount = qdict_get_try_int(qdict, "icount", -1LL);
    Error *err = NULL;

    qmp_replay_break(icount, &err);
    if (err) {
        error_report_err(err);
        return;
    }
}

void qmp_replay_delete_break(Error **errp)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_delete_break();
    } else {
        error_setg(errp, "replay breakpoints are allowed only in play mode");
    }
}

void hmp_replay_delete_break(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_replay_delete_break(&err);
    if (err) {
        error_report_err(err);
        return;
    }
}
