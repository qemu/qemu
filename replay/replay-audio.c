/*
 * replay-audio.c
 *
 * Copyright (c) 2010-2017 Institute for System Programming
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
#include "audio/audio.h"

void replay_audio_out(size_t *played)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_instructions();
        replay_put_event(EVENT_AUDIO_OUT);
        replay_put_qword(*played);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        if (replay_next_event_is(EVENT_AUDIO_OUT)) {
            *played = replay_get_qword();
            replay_finish_event();
        } else {
            error_report("Missing audio out event in the replay log");
            abort();
        }
    }
}

void replay_audio_in(size_t *recorded, void *samples, size_t *wpos, size_t size)
{
    int pos;
    uint64_t left, right;
    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_instructions();
        replay_put_event(EVENT_AUDIO_IN);
        replay_put_qword(*recorded);
        replay_put_qword(*wpos);
        for (pos = (*wpos - *recorded + size) % size ; pos != *wpos
             ; pos = (pos + 1) % size) {
            audio_sample_to_uint64(samples, pos, &left, &right);
            replay_put_qword(left);
            replay_put_qword(right);
        }
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        if (replay_next_event_is(EVENT_AUDIO_IN)) {
            *recorded = replay_get_qword();
            *wpos = replay_get_qword();
            for (pos = (*wpos - *recorded + size) % size ; pos != *wpos
                 ; pos = (pos + 1) % size) {
                left = replay_get_qword();
                right = replay_get_qword();
                audio_sample_from_uint64(samples, pos, left, right);
            }
            replay_finish_event();
        } else {
            error_report("Missing audio in event in the replay log");
            abort();
        }
    }
}
