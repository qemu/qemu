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
#include "system/replay.h"
#include "replay-internal.h"

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

void replay_audio_in_start(size_t *nsamples)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_instructions();
        replay_put_event(EVENT_AUDIO_IN);
        replay_put_qword(*nsamples);
        replay_state.n_audio_samples = *nsamples;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        if (replay_next_event_is(EVENT_AUDIO_IN)) {
            *nsamples = replay_get_qword();
            replay_state.n_audio_samples = *nsamples;
        } else {
            error_report("Missing audio in event in the replay log");
            abort();
        }
    }
}

void replay_audio_in_sample_lr(uint64_t *left, uint64_t *right)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_put_qword(*left);
        replay_put_qword(*right);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        *left = replay_get_qword();
        *right = replay_get_qword();
    } else {
        return;
    }

    assert(replay_state.n_audio_samples > 0);
    replay_state.n_audio_samples--;
}

void replay_audio_in_finish(void)
{
    assert(replay_state.n_audio_samples == 0);

    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_finish_event();
    }
}
