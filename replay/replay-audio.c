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
#include "sysemu/sysemu.h"
#include "audio/audio.h"

void replay_audio_out(int *played)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_mutex_lock();
        replay_put_event(EVENT_AUDIO_OUT);
        replay_put_dword(*played);
        replay_mutex_unlock();
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        replay_account_executed_instructions();
        replay_mutex_lock();
        if (replay_next_event_is(EVENT_AUDIO_OUT)) {
            *played = replay_get_dword();
            replay_finish_event();
            replay_mutex_unlock();
        } else {
            replay_mutex_unlock();
            error_report("Missing audio out event in the replay log");
            abort();
        }
    }
}

void replay_audio_in(int *recorded, void *samples, int *wpos, int size)
{
    int pos;
    uint64_t left, right;
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_mutex_lock();
        replay_put_event(EVENT_AUDIO_IN);
        replay_put_dword(*recorded);
        replay_put_dword(*wpos);
        for (pos = (*wpos - *recorded + size) % size ; pos != *wpos
             ; pos = (pos + 1) % size) {
            audio_sample_to_uint64(samples, pos, &left, &right);
            replay_put_qword(left);
            replay_put_qword(right);
        }
        replay_mutex_unlock();
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        replay_account_executed_instructions();
        replay_mutex_lock();
        if (replay_next_event_is(EVENT_AUDIO_IN)) {
            *recorded = replay_get_dword();
            *wpos = replay_get_dword();
            for (pos = (*wpos - *recorded + size) % size ; pos != *wpos
                 ; pos = (pos + 1) % size) {
                left = replay_get_qword();
                right = replay_get_qword();
                audio_sample_from_uint64(samples, pos, left, right);
            }
            replay_finish_event();
            replay_mutex_unlock();
        } else {
            replay_mutex_unlock();
            error_report("Missing audio in event in the replay log");
            abort();
        }
    }
}
