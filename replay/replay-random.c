/*
 * replay-random.c
 *
 * Copyright (c) 2010-2020 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/replay.h"
#include "replay-internal.h"

void replay_save_random(int ret, void *buf, size_t len)
{
    g_assert(replay_mutex_locked());

    replay_save_instructions();
    replay_put_event(EVENT_RANDOM);
    replay_put_dword(ret);
    replay_put_array(buf, len);
}

int replay_read_random(void *buf, size_t len)
{
    int ret = 0;
    g_assert(replay_mutex_locked());

    replay_account_executed_instructions();
    if (replay_next_event_is(EVENT_RANDOM)) {
        size_t buf_size = 0;
        ret = replay_get_dword();
        replay_get_array(buf, &buf_size);
        replay_finish_event();
        g_assert(buf_size == len);
    } else {
        error_report("Missing random event in the replay log");
        exit(1);
    }
    return ret;
}
