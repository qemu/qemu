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
#include "system/replay.h"
#include "system/runstate.h"
#include "replay-internal.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-replay.h"
#include "qobject/qdict.h"
#include "qemu/timer.h"
#include "block/snapshot.h"
#include "migration/snapshot.h"

static bool replay_is_debugging;
static int64_t replay_last_breakpoint;
static int64_t replay_last_snapshot;

bool replay_running_debug(void)
{
    return replay_is_debugging;
}

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

static char *replay_find_nearest_snapshot(int64_t icount,
                                          int64_t *snapshot_icount)
{
    BlockDriverState *bs;
    QEMUSnapshotInfo *sn_tab;
    QEMUSnapshotInfo *nearest = NULL;
    char *ret = NULL;
    int rv;
    int nb_sns, i;

    *snapshot_icount = -1;

    bs = bdrv_all_find_vmstate_bs(NULL, false, NULL, NULL);
    if (!bs) {
        goto fail;
    }

    nb_sns = bdrv_snapshot_list(bs, &sn_tab);

    for (i = 0; i < nb_sns; i++) {
        rv = bdrv_all_has_snapshot(sn_tab[i].name, false, NULL, NULL);
        if (rv < 0)
            goto fail;
        if (rv == 1) {
            if (sn_tab[i].icount != -1ULL
                && sn_tab[i].icount <= icount
                && (!nearest || nearest->icount < sn_tab[i].icount)) {
                nearest = &sn_tab[i];
            }
        }
    }
    if (nearest) {
        ret = g_strdup(nearest->name);
        *snapshot_icount = nearest->icount;
    }
    g_free(sn_tab);

fail:
    return ret;
}

static void replay_seek(int64_t icount, QEMUTimerCB callback, Error **errp)
{
    char *snapshot = NULL;
    int64_t snapshot_icount;

    if (replay_mode != REPLAY_MODE_PLAY) {
        error_setg(errp, "replay must be enabled to seek");
        return;
    }

    snapshot = replay_find_nearest_snapshot(icount, &snapshot_icount);
    if (snapshot) {
        if (icount < replay_get_current_icount()
            || replay_get_current_icount() < snapshot_icount) {
            vm_stop(RUN_STATE_RESTORE_VM);
            load_snapshot(snapshot, NULL, false, NULL, errp);
        }
        g_free(snapshot);
    }
    if (replay_get_current_icount() <= icount) {
        replay_break(icount, callback, NULL);
        vm_start();
    } else {
        error_setg(errp, "cannot seek to the specified instruction count");
    }
}

void qmp_replay_seek(int64_t icount, Error **errp)
{
    replay_seek(icount, replay_stop_vm, errp);
}

void hmp_replay_seek(Monitor *mon, const QDict *qdict)
{
    int64_t icount = qdict_get_try_int(qdict, "icount", -1LL);
    Error *err = NULL;

    qmp_replay_seek(icount, &err);
    if (err) {
        error_report_err(err);
        return;
    }
}

static void replay_stop_vm_debug(void *opaque)
{
    replay_is_debugging = false;
    vm_stop(RUN_STATE_DEBUG);
    replay_delete_break();
}

bool replay_reverse_step(void)
{
    Error *err = NULL;

    assert(replay_mode == REPLAY_MODE_PLAY);

    if (replay_get_current_icount() != 0) {
        replay_seek(replay_get_current_icount() - 1,
                    replay_stop_vm_debug, &err);
        if (err) {
            error_free(err);
            return false;
        }
        replay_is_debugging = true;
        return true;
    }

    return false;
}

static void replay_continue_end(void)
{
    replay_is_debugging = false;
    vm_stop(RUN_STATE_DEBUG);
    replay_delete_break();
}

static void replay_continue_stop(void *opaque)
{
    Error *err = NULL;
    if (replay_last_breakpoint != -1LL) {
        replay_seek(replay_last_breakpoint, replay_stop_vm_debug, &err);
        if (err) {
            error_free(err);
            replay_continue_end();
        }
        return;
    }
    /*
     * No breakpoints since the last snapshot.
     * Find previous snapshot and try again.
     */
    if (replay_last_snapshot != 0) {
        replay_seek(replay_last_snapshot - 1, replay_continue_stop, &err);
        if (err) {
            error_free(err);
            replay_continue_end();
        }
        replay_last_snapshot = replay_get_current_icount();
    } else {
        /* Seek to the very first step */
        replay_seek(0, replay_stop_vm_debug, &err);
        if (err) {
            error_free(err);
            replay_continue_end();
        }
    }
}

bool replay_reverse_continue(void)
{
    Error *err = NULL;

    assert(replay_mode == REPLAY_MODE_PLAY);

    if (replay_get_current_icount() != 0) {
        replay_seek(replay_get_current_icount() - 1,
                    replay_continue_stop, &err);
        if (err) {
            error_free(err);
            return false;
        }
        replay_last_breakpoint = -1LL;
        replay_is_debugging = true;
        replay_last_snapshot = replay_get_current_icount();
        return true;
    }

    return false;
}

void replay_breakpoint(void)
{
    assert(replay_mode == REPLAY_MODE_PLAY);
    replay_last_breakpoint = replay_get_current_icount();
}

void replay_gdb_attached(void)
{
    /*
     * Create VM snapshot on temporary overlay to allow reverse
     * debugging even if snapshots were not enabled.
     */
    if (replay_mode == REPLAY_MODE_PLAY
        && !replay_snapshot) {
        if (!save_snapshot("start_debugging", true, NULL, false, NULL, NULL)) {
            /* Can't create the snapshot. Continue conventional debugging. */
        }
    }
}
