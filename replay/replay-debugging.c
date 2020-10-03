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
#include "replay-internal.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-replay.h"

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
