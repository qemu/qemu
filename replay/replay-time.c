/*
 * replay-time.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "qemu/error-report.h"

int64_t replay_save_clock(ReplayClockKind kind, int64_t clock, int64_t raw_icount)
{
    if (replay_file) {
        g_assert(replay_mutex_locked());

        /* Due to the caller's locking requirements we get the icount from it
         * instead of using replay_save_instructions().
         */
        replay_advance_current_step(raw_icount);
        replay_put_event(EVENT_CLOCK + kind);
        replay_put_qword(clock);
    }

    return clock;
}

void replay_read_next_clock(ReplayClockKind kind)
{
    unsigned int read_kind = replay_state.data_kind - EVENT_CLOCK;

    assert(read_kind == kind);

    int64_t clock = replay_get_qword();

    replay_check_error();
    replay_finish_event();

    replay_state.cached_clock[read_kind] = clock;
}

/*! Reads next clock event from the input. */
int64_t replay_read_clock(ReplayClockKind kind)
{
    g_assert(replay_file && replay_mutex_locked());

    replay_account_executed_instructions();

    if (replay_file) {
        int64_t ret;
        if (replay_next_event_is(EVENT_CLOCK + kind)) {
            replay_read_next_clock(kind);
        }
        ret = replay_state.cached_clock[kind];

        return ret;
    }

    error_report("REPLAY INTERNAL ERROR %d", __LINE__);
    exit(1);
}
