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
#include "qemu-common.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "qapi/qmp/qstring.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"

static void replay_pre_save(void *opaque)
{
    ReplayState *state = opaque;
    state->file_offset = ftell(replay_file);
}

static int replay_post_load(void *opaque, int version_id)
{
    ReplayState *state = opaque;
    fseek(replay_file, state->file_offset, SEEK_SET);
    /* If this was a vmstate, saved in recording mode,
       we need to initialize replay data fields. */
    replay_fetch_data_kind();

    return 0;
}

static const VMStateDescription vmstate_replay = {
    .name = "replay",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = replay_pre_save,
    .post_load = replay_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT64_ARRAY(cached_clock, ReplayState, REPLAY_CLOCK_COUNT),
        VMSTATE_UINT64(current_step, ReplayState),
        VMSTATE_INT32(instructions_count, ReplayState),
        VMSTATE_UINT32(data_kind, ReplayState),
        VMSTATE_UINT32(has_unread_data, ReplayState),
        VMSTATE_UINT64(file_offset, ReplayState),
        VMSTATE_UINT64(block_request_id, ReplayState),
        VMSTATE_END_OF_LIST()
    },
};

void replay_vmstate_register(void)
{
    vmstate_register(NULL, 0, &vmstate_replay, &replay_state);
}

void replay_vmstate_init(void)
{
    if (replay_snapshot) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            if (save_vmstate(cur_mon, replay_snapshot) != 0) {
                error_report("Could not create snapshot for icount record");
                exit(1);
            }
        } else if (replay_mode == REPLAY_MODE_PLAY) {
            if (load_vmstate(replay_snapshot) != 0) {
                error_report("Could not load snapshot for icount replay");
                exit(1);
            }
        }
    }
}
