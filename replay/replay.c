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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

/* Current version of the replay mechanism.
   Increase it when file format changes. */
#define REPLAY_VERSION              0xe02006
/* Size of replay log header */
#define HEADER_SIZE                 (sizeof(uint32_t) + sizeof(uint64_t))

ReplayMode replay_mode = REPLAY_MODE_NONE;
char *replay_snapshot;

/* Name of replay file  */
static char *replay_filename;
ReplayState replay_state;
static GSList *replay_blockers;

bool replay_next_event_is(int event)
{
    bool res = false;

    /* nothing to skip - not all instructions used */
    if (replay_state.instructions_count != 0) {
        assert(replay_state.data_kind == EVENT_INSTRUCTION);
        return event == EVENT_INSTRUCTION;
    }

    while (true) {
        if (event == replay_state.data_kind) {
            res = true;
        }
        switch (replay_state.data_kind) {
        case EVENT_SHUTDOWN:
            replay_finish_event();
            qemu_system_shutdown_request();
            break;
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

int replay_get_instructions(void)
{
    int res = 0;
    replay_mutex_lock();
    if (replay_next_event_is(EVENT_INSTRUCTION)) {
        res = replay_state.instructions_count;
    }
    replay_mutex_unlock();
    return res;
}

void replay_account_executed_instructions(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_mutex_lock();
        if (replay_state.instructions_count > 0) {
            int count = (int)(replay_get_current_step()
                              - replay_state.current_step);

            /* Time can only go forward */
            assert(count >= 0);

            replay_state.instructions_count -= count;
            replay_state.current_step += count;
            if (replay_state.instructions_count == 0) {
                assert(replay_state.data_kind == EVENT_INSTRUCTION);
                replay_finish_event();
                /* Wake up iothread. This is required because
                   timers will not expire until clock counters
                   will be read from the log. */
                qemu_notify_event();
            }
        }
        replay_mutex_unlock();
    }
}

bool replay_exception(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_mutex_lock();
        replay_put_event(EVENT_EXCEPTION);
        replay_mutex_unlock();
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        bool res = replay_has_exception();
        if (res) {
            replay_mutex_lock();
            replay_finish_event();
            replay_mutex_unlock();
        }
        return res;
    }

    return true;
}

bool replay_has_exception(void)
{
    bool res = false;
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_account_executed_instructions();
        replay_mutex_lock();
        res = replay_next_event_is(EVENT_EXCEPTION);
        replay_mutex_unlock();
    }

    return res;
}

bool replay_interrupt(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_instructions();
        replay_mutex_lock();
        replay_put_event(EVENT_INTERRUPT);
        replay_mutex_unlock();
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        bool res = replay_has_interrupt();
        if (res) {
            replay_mutex_lock();
            replay_finish_event();
            replay_mutex_unlock();
        }
        return res;
    }

    return true;
}

bool replay_has_interrupt(void)
{
    bool res = false;
    if (replay_mode == REPLAY_MODE_PLAY) {
        replay_account_executed_instructions();
        replay_mutex_lock();
        res = replay_next_event_is(EVENT_INTERRUPT);
        replay_mutex_unlock();
    }
    return res;
}

void replay_shutdown_request(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_mutex_lock();
        replay_put_event(EVENT_SHUTDOWN);
        replay_mutex_unlock();
    }
}

bool replay_checkpoint(ReplayCheckpoint checkpoint)
{
    bool res = false;
    assert(EVENT_CHECKPOINT + checkpoint <= EVENT_CHECKPOINT_LAST);
    replay_save_instructions();

    if (!replay_file) {
        return true;
    }

    replay_mutex_lock();

    if (replay_mode == REPLAY_MODE_PLAY) {
        if (replay_next_event_is(EVENT_CHECKPOINT + checkpoint)) {
            replay_finish_event();
        } else if (replay_state.data_kind != EVENT_ASYNC) {
            res = false;
            goto out;
        }
        replay_read_events(checkpoint);
        /* replay_read_events may leave some unread events.
           Return false if not all of the events associated with
           checkpoint were processed */
        res = replay_state.data_kind != EVENT_ASYNC;
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        replay_put_event(EVENT_CHECKPOINT + checkpoint);
        replay_save_events(checkpoint);
        res = true;
    }
out:
    replay_mutex_unlock();
    return res;
}

static void replay_enable(const char *fname, int mode)
{
    const char *fmode = NULL;
    assert(!replay_file);

    switch (mode) {
    case REPLAY_MODE_RECORD:
        fmode = "wb";
        break;
    case REPLAY_MODE_PLAY:
        fmode = "rb";
        break;
    default:
        fprintf(stderr, "Replay: internal error: invalid replay mode\n");
        exit(1);
    }

    atexit(replay_finish);

    replay_mutex_init();

    replay_file = fopen(fname, fmode);
    if (replay_file == NULL) {
        fprintf(stderr, "Replay: open %s: %s\n", fname, strerror(errno));
        exit(1);
    }

    replay_filename = g_strdup(fname);

    replay_mode = mode;
    replay_state.data_kind = -1;
    replay_state.instructions_count = 0;
    replay_state.current_step = 0;
    replay_state.has_unread_data = 0;

    /* skip file header for RECORD and check it for PLAY */
    if (replay_mode == REPLAY_MODE_RECORD) {
        fseek(replay_file, HEADER_SIZE, SEEK_SET);
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        unsigned int version = replay_get_dword();
        if (version != REPLAY_VERSION) {
            fprintf(stderr, "Replay: invalid input log file version\n");
            exit(1);
        }
        /* go to the beginning */
        fseek(replay_file, HEADER_SIZE, SEEK_SET);
        replay_fetch_data_kind();
    }

    replay_init_events();
}

void replay_configure(QemuOpts *opts)
{
    const char *fname;
    const char *rr;
    ReplayMode mode = REPLAY_MODE_NONE;
    Location loc;

    if (!opts) {
        return;
    }

    loc_push_none(&loc);
    qemu_opts_loc_restore(opts);

    rr = qemu_opt_get(opts, "rr");
    if (!rr) {
        /* Just enabling icount */
        goto out;
    } else if (!strcmp(rr, "record")) {
        mode = REPLAY_MODE_RECORD;
    } else if (!strcmp(rr, "replay")) {
        mode = REPLAY_MODE_PLAY;
    } else {
        error_report("Invalid icount rr option: %s", rr);
        exit(1);
    }

    fname = qemu_opt_get(opts, "rrfile");
    if (!fname) {
        error_report("File name not specified for replay");
        exit(1);
    }

    replay_snapshot = g_strdup(qemu_opt_get(opts, "rrsnapshot"));
    replay_vmstate_register();
    replay_enable(fname, mode);

out:
    loc_pop(&loc);
}

void replay_start(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    if (replay_blockers) {
        error_reportf_err(replay_blockers->data, "Record/replay: ");
        exit(1);
    }
    if (!use_icount) {
        error_report("Please enable icount to use record/replay");
        exit(1);
    }

    /* Timer for snapshotting will be set up here. */

    replay_enable_events();
}

void replay_finish(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    replay_save_instructions();

    /* finalize the file */
    if (replay_file) {
        if (replay_mode == REPLAY_MODE_RECORD) {
            /* write end event */
            replay_put_event(EVENT_END);

            /* write header */
            fseek(replay_file, 0, SEEK_SET);
            replay_put_dword(REPLAY_VERSION);
        }

        fclose(replay_file);
        replay_file = NULL;
    }
    if (replay_filename) {
        g_free(replay_filename);
        replay_filename = NULL;
    }

    g_free(replay_snapshot);
    replay_snapshot = NULL;

    replay_finish_events();
    replay_mutex_destroy();
}

void replay_add_blocker(Error *reason)
{
    replay_blockers = g_slist_prepend(replay_blockers, reason);
}
