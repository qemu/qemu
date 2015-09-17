/*
 * replay.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "qemu/timer.h"

ReplayMode replay_mode = REPLAY_MODE_NONE;

ReplayState replay_state;

bool replay_next_event_is(int event)
{
    bool res = false;

    /* nothing to skip - not all instructions used */
    if (replay_state.instructions_count != 0) {
        assert(replay_data_kind == EVENT_INSTRUCTION);
        return event == EVENT_INSTRUCTION;
    }

    while (true) {
        if (event == replay_data_kind) {
            res = true;
        }
        switch (replay_data_kind) {
        default:
            /* clock, time_t, checkpoint and other events */
            return res;
        }
    }
    return res;
}

uint64_t replay_get_current_step(void)
{
    return cpu_get_icount_raw();
}
