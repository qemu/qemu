/*
 * Global State configuration
 *
 * Copyright (c) 2014-2017 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "sysemu/runstate.h"
#include "qapi/error.h"
#include "migration.h"
#include "migration/global_state.h"
#include "migration/vmstate.h"
#include "trace.h"

typedef struct {
    uint32_t size;

    /*
     * runstate was 100 bytes, zero padded, but we trimmed it to add a
     * few fields and maintain backwards compatibility.
     */
    uint8_t runstate[32];
    uint8_t has_vm_was_suspended;
    uint8_t vm_was_suspended;
    uint8_t unused[66];

    RunState state;
    bool received;
} GlobalState;

static GlobalState global_state;

static void global_state_do_store(RunState state)
{
    const char *state_str = RunState_str(state);
    assert(strlen(state_str) < sizeof(global_state.runstate));
    strpadcpy((char *)global_state.runstate, sizeof(global_state.runstate),
              state_str, '\0');
    global_state.has_vm_was_suspended = true;
    global_state.vm_was_suspended = vm_get_suspended();

    memset(global_state.unused, 0, sizeof(global_state.unused));
}

void global_state_store(void)
{
    global_state_do_store(runstate_get());
}

void global_state_store_running(void)
{
    global_state_do_store(RUN_STATE_RUNNING);
}

bool global_state_received(void)
{
    return global_state.received;
}

RunState global_state_get_runstate(void)
{
    return global_state.state;
}

static bool global_state_needed(void *opaque)
{
    return migrate_get_current()->store_global_state;
}

static int global_state_post_load(void *opaque, int version_id)
{
    GlobalState *s = opaque;
    Error *local_err = NULL;
    int r;
    char *runstate = (char *)s->runstate;

    s->received = true;
    trace_migrate_global_state_post_load(runstate);

    if (strnlen((char *)s->runstate,
                sizeof(s->runstate)) == sizeof(s->runstate)) {
        /*
         * This condition should never happen during migration, because
         * all runstate names are shorter than 32 bytes (the size of
         * s->runstate). However, a malicious stream could overflow
         * the qapi_enum_parse() call, so we force the last character
         * to a NUL byte.
         */
        s->runstate[sizeof(s->runstate) - 1] = '\0';
    }
    r = qapi_enum_parse(&RunState_lookup, runstate, -1, &local_err);

    if (r == -1) {
        if (local_err) {
            error_report_err(local_err);
        }
        return -EINVAL;
    }
    s->state = r;

    /*
     * global_state is saved on the outgoing side before forcing a stopped
     * state, so it may have saved state=suspended and vm_was_suspended=0.
     * Now we are in a paused state, and when we later call vm_start, it must
     * restore the suspended state, so we must set vm_was_suspended=1 here.
     */
    vm_set_suspended(s->vm_was_suspended || r == RUN_STATE_SUSPENDED);

    return 0;
}

static int global_state_pre_save(void *opaque)
{
    GlobalState *s = opaque;

    trace_migrate_global_state_pre_save((char *)s->runstate);
    s->size = strnlen((char *)s->runstate, sizeof(s->runstate)) + 1;
    assert(s->size <= sizeof(s->runstate));

    return 0;
}

static const VMStateDescription vmstate_globalstate = {
    .name = "globalstate",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = global_state_post_load,
    .pre_save = global_state_pre_save,
    .needed = global_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(size, GlobalState),
        VMSTATE_BUFFER(runstate, GlobalState),
        VMSTATE_UINT8(has_vm_was_suspended, GlobalState),
        VMSTATE_UINT8(vm_was_suspended, GlobalState),
        VMSTATE_BUFFER(unused, GlobalState),
        VMSTATE_END_OF_LIST()
    },
};

void register_global_state(void)
{
    /* We would use it independently that we receive it */
    strcpy((char *)&global_state.runstate, "");
    global_state.received = false;
    vmstate_register(NULL, 0, &vmstate_globalstate, &global_state);
}
