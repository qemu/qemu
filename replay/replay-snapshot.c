/*
 * replay-snapshot.c
 *
 * Copyright (c) 2010-2016 Institute for System Programming
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
#include "monitor/monitor.h"
#include "qapi/qmp/qstring.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "migration/snapshot.h"

static int replay_pre_save(void *opaque)
{
    ReplayState *state = opaque;
    state->file_offset = ftell(replay_file);

    return 0;
}

static int replay_post_load(void *opaque, int version_id)
{
    ReplayState *state = opaque;
    if (replay_mode == REPLAY_MODE_PLAY) {
        fseek(replay_file, state->file_offset, SEEK_SET);
        /* If this was a vmstate, saved in recording mode,
           we need to initialize replay data fields. */
        replay_fetch_data_kind();
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        /* This is only useful for loading the initial state.
           Therefore reset all the counters. */
        state->instruction_count = 0;
        state->block_request_id = 0;
    }

    return 0;
}

static const VMStateDescription vmstate_replay = {
    .name = "replay",
    .version_id = 3,
    .minimum_version_id = 3,
    .pre_save = replay_pre_save,
    .post_load = replay_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64_ARRAY(cached_clock, ReplayState, REPLAY_CLOCK_COUNT),
        VMSTATE_UINT64(current_icount, ReplayState),
        VMSTATE_INT32(instruction_count, ReplayState),
        VMSTATE_UINT32(current_event, ReplayState),
        VMSTATE_UINT32(data_kind, ReplayState),
        VMSTATE_BOOL(has_unread_data, ReplayState),
        VMSTATE_UINT64(file_offset, ReplayState),
        VMSTATE_UINT64(block_request_id, ReplayState),
        VMSTATE_UINT64(read_event_id, ReplayState),
        VMSTATE_END_OF_LIST()
    },
};

void replay_vmstate_register(void)
{
    vmstate_register(NULL, 0, &vmstate_replay, &replay_state);
}

void replay_vmstate_init(void)
{
    Error *err = NULL;

    if (replay_snapshot) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            if (!save_snapshot(replay_snapshot,
                               true, NULL, false, NULL, &err)) {
                error_report_err(err);
                error_report("Could not create snapshot for icount record");
                exit(1);
            }
        } else if (replay_mode == REPLAY_MODE_PLAY) {
            if (!load_snapshot(replay_snapshot, NULL, false, NULL, &err)) {
                error_report_err(err);
                error_report("Could not load snapshot for icount replay");
                exit(1);
            }
        }
    }
}

bool replay_can_snapshot(void)
{
    return replay_mode == REPLAY_MODE_NONE
        || !replay_has_events();
}
