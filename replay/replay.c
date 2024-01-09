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
#include "sysemu/cpu-timers.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"
#include "replay-internal.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "sysemu/cpus.h"
#include "qemu/error-report.h"

/* Current version of the replay mechanism.
   Increase it when file format changes. */
#define REPLAY_VERSION              0xe0200c
/* Size of replay log header */
#define HEADER_SIZE                 (sizeof(uint32_t) + sizeof(uint64_t))

ReplayMode replay_mode = REPLAY_MODE_NONE;
char *replay_snapshot;

/* Name of replay file  */
static char *replay_filename;
ReplayState replay_state;
static GSList *replay_blockers;

/* Replay breakpoints */
uint64_t replay_break_icount = -1ULL;
QEMUTimer *replay_break_timer;

/* Pretty print event names */

static const char *replay_async_event_name(ReplayAsyncEventKind event)
{
    switch (event) {
#define ASYNC_EVENT(_x) case REPLAY_ASYNC_EVENT_ ## _x: return "ASYNC_EVENT_"#_x
        ASYNC_EVENT(BH);
        ASYNC_EVENT(BH_ONESHOT);
        ASYNC_EVENT(INPUT);
        ASYNC_EVENT(INPUT_SYNC);
        ASYNC_EVENT(CHAR_READ);
        ASYNC_EVENT(BLOCK);
        ASYNC_EVENT(NET);
#undef ASYNC_EVENT
    default:
        g_assert_not_reached();
    }
}

static const char *replay_clock_event_name(ReplayClockKind clock)
{
    switch (clock) {
#define CLOCK_EVENT(_x) case REPLAY_CLOCK_ ## _x: return "CLOCK_" #_x
        CLOCK_EVENT(HOST);
        CLOCK_EVENT(VIRTUAL_RT);
#undef CLOCK_EVENT
    default:
        g_assert_not_reached();
    }
}

/* Pretty print shutdown event names */
static const char *replay_shutdown_event_name(ShutdownCause cause)
{
    switch (cause) {
#define SHUTDOWN_EVENT(_x) case SHUTDOWN_CAUSE_ ## _x: return "SHUTDOWN_CAUSE_" #_x
        SHUTDOWN_EVENT(NONE);
        SHUTDOWN_EVENT(HOST_ERROR);
        SHUTDOWN_EVENT(HOST_QMP_QUIT);
        SHUTDOWN_EVENT(HOST_QMP_SYSTEM_RESET);
        SHUTDOWN_EVENT(HOST_SIGNAL);
        SHUTDOWN_EVENT(HOST_UI);
        SHUTDOWN_EVENT(GUEST_SHUTDOWN);
        SHUTDOWN_EVENT(GUEST_RESET);
        SHUTDOWN_EVENT(GUEST_PANIC);
        SHUTDOWN_EVENT(SUBSYSTEM_RESET);
        SHUTDOWN_EVENT(SNAPSHOT_LOAD);
#undef SHUTDOWN_EVENT
    default:
        g_assert_not_reached();
    }
}

static const char *replay_checkpoint_event_name(enum ReplayCheckpoint checkpoint)
{
    switch (checkpoint) {
#define CHECKPOINT_EVENT(_x) case CHECKPOINT_ ## _x: return "CHECKPOINT_" #_x
        CHECKPOINT_EVENT(CLOCK_WARP_START);
        CHECKPOINT_EVENT(CLOCK_WARP_ACCOUNT);
        CHECKPOINT_EVENT(RESET_REQUESTED);
        CHECKPOINT_EVENT(SUSPEND_REQUESTED);
        CHECKPOINT_EVENT(CLOCK_VIRTUAL);
        CHECKPOINT_EVENT(CLOCK_HOST);
        CHECKPOINT_EVENT(CLOCK_VIRTUAL_RT);
        CHECKPOINT_EVENT(INIT);
        CHECKPOINT_EVENT(RESET);
#undef CHECKPOINT_EVENT
    default:
        g_assert_not_reached();
    }
}

static const char *replay_event_name(enum ReplayEvents event)
{
    /* First deal with the simple ones */
    switch (event) {
#define EVENT(_x) case EVENT_ ## _x: return "EVENT_"#_x
        EVENT(INSTRUCTION);
        EVENT(INTERRUPT);
        EVENT(EXCEPTION);
        EVENT(CHAR_WRITE);
        EVENT(CHAR_READ_ALL);
        EVENT(AUDIO_OUT);
        EVENT(AUDIO_IN);
        EVENT(RANDOM);
#undef EVENT
    default:
        if (event >= EVENT_ASYNC && event <= EVENT_ASYNC_LAST) {
            return replay_async_event_name(event - EVENT_ASYNC);
        } else if (event >= EVENT_SHUTDOWN && event <= EVENT_SHUTDOWN_LAST) {
            return replay_shutdown_event_name(event - EVENT_SHUTDOWN);
        } else if (event >= EVENT_CLOCK && event <= EVENT_CLOCK_LAST) {
            return replay_clock_event_name(event - EVENT_CLOCK);
        } else if (event >= EVENT_CHECKPOINT && event <= EVENT_CHECKPOINT_LAST) {
            return replay_checkpoint_event_name(event - EVENT_CHECKPOINT);
        }
    }

    g_assert_not_reached();
}

bool replay_next_event_is(int event)
{
    bool res = false;

    /* nothing to skip - not all instructions used */
    if (replay_state.instruction_count != 0) {
        assert(replay_state.data_kind == EVENT_INSTRUCTION);
        return event == EVENT_INSTRUCTION;
    }

    while (true) {
        unsigned int data_kind = replay_state.data_kind;
        if (event == data_kind) {
            res = true;
        }
        switch (data_kind) {
        case EVENT_SHUTDOWN ... EVENT_SHUTDOWN_LAST:
            replay_finish_event();
            qemu_system_shutdown_request(data_kind - EVENT_SHUTDOWN);
            break;
        default:
            /* clock, time_t, checkpoint and other events */
            return res;
        }
    }
    return res;
}

uint64_t replay_get_current_icount(void)
{
    return icount_get_raw();
}

int replay_get_instructions(void)
{
    int res = 0;
    g_assert(replay_mutex_locked());
    if (replay_next_event_is(EVENT_INSTRUCTION)) {
        res = replay_state.instruction_count;
        if (replay_break_icount != -1LL) {
            uint64_t current = replay_get_current_icount();
            assert(replay_break_icount >= current);
            if (current + res > replay_break_icount) {
                res = replay_break_icount - current;
            }
        }
    }
    return res;
}

void replay_account_executed_instructions(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        if (replay_state.instruction_count > 0) {
            replay_advance_current_icount(replay_get_current_icount());
        }
    }
}

bool replay_exception(void)
{

    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_instructions();
        replay_put_event(EVENT_EXCEPTION);
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        bool res = replay_has_exception();
        if (res) {
            replay_finish_event();
        }
        return res;
    }

    return true;
}

bool replay_has_exception(void)
{
    bool res = false;
    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        res = replay_next_event_is(EVENT_EXCEPTION);
    }

    return res;
}

bool replay_interrupt(void)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_instructions();
        replay_put_event(EVENT_INTERRUPT);
        return true;
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        bool res = replay_has_interrupt();
        if (res) {
            replay_finish_event();
        }
        return res;
    }

    return true;
}

bool replay_has_interrupt(void)
{
    bool res = false;
    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        res = replay_next_event_is(EVENT_INTERRUPT);
    }
    return res;
}

void replay_shutdown_request(ShutdownCause cause)
{
    if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_put_event(EVENT_SHUTDOWN + cause);
    }
}

bool replay_checkpoint(ReplayCheckpoint checkpoint)
{
    assert(EVENT_CHECKPOINT + checkpoint <= EVENT_CHECKPOINT_LAST);

    replay_save_instructions();

    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        if (replay_next_event_is(EVENT_CHECKPOINT + checkpoint)) {
            replay_finish_event();
        } else {
            return false;
        }
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_put_event(EVENT_CHECKPOINT + checkpoint);
    }
    return true;
}

void replay_async_events(void)
{
    static bool processing = false;
    /*
     * If we are already processing the events, recursion may occur
     * in case of incorrect implementation when HW event modifies timers.
     * Timer modification may invoke the icount warp, event processing,
     * and cause the recursion.
     */
    g_assert(!processing);
    processing = true;

    replay_save_instructions();

    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_read_events();
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_save_events();
    }
    processing = false;
}

bool replay_has_event(void)
{
    bool res = false;
    if (replay_mode == REPLAY_MODE_PLAY) {
        g_assert(replay_mutex_locked());
        replay_account_executed_instructions();
        res = EVENT_CHECKPOINT <= replay_state.data_kind
              && replay_state.data_kind <= EVENT_CHECKPOINT_LAST;
        res = res || (EVENT_ASYNC <= replay_state.data_kind
                     && replay_state.data_kind <= EVENT_ASYNC_LAST);
    }
    return res;
}

G_NORETURN void replay_sync_error(const char *error)
{
    error_report("%s (insn total %"PRId64"/%d left, event %d is %s)", error,
                 replay_state.current_icount, replay_state.instruction_count,
                 replay_state.current_event,
                 replay_event_name(replay_state.data_kind));
    abort();
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

    replay_file = fopen(fname, fmode);
    if (replay_file == NULL) {
        fprintf(stderr, "Replay: open %s: %s\n", fname, strerror(errno));
        exit(1);
    }

    replay_filename = g_strdup(fname);
    replay_mode = mode;
    replay_mutex_init();

    replay_state.data_kind = -1;
    replay_state.instruction_count = 0;
    replay_state.current_icount = 0;
    replay_state.current_event = 0;
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
    if (!icount_enabled()) {
        error_report("Please enable icount to use record/replay");
        exit(1);
    }

    /* Timer for snapshotting will be set up here. */

    replay_enable_events();
}

/*
 * For none/record the answer is yes.
 */
bool replay_can_wait(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /*
         * For playback we shouldn't ever be at a point we wait. If
         * the instruction count has reached zero and we have an
         * unconsumed event we should go around again and consume it.
         */
        if (replay_state.instruction_count == 0 && replay_state.has_unread_data) {
            return false;
        } else {
            replay_sync_error("Playback shouldn't have to iowait");
        }
    }
    return true;
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
            /*
             * Can't do it in the signal handler, therefore
             * add shutdown event here for the case of Ctrl-C.
             */
            replay_shutdown_request(SHUTDOWN_CAUSE_HOST_SIGNAL);
            /* write end event */
            replay_put_event(EVENT_END);

            /* write header */
            fseek(replay_file, 0, SEEK_SET);
            replay_put_dword(REPLAY_VERSION);
        }

        fclose(replay_file);
        replay_file = NULL;
    }
    g_free(replay_filename);
    replay_filename = NULL;

    g_free(replay_snapshot);
    replay_snapshot = NULL;

    replay_finish_events();
    replay_mode = REPLAY_MODE_NONE;
}

void replay_add_blocker(const char *feature)
{
    Error *reason = NULL;

    error_setg(&reason, "Record/replay feature is not supported for '%s'",
               feature);
    replay_blockers = g_slist_prepend(replay_blockers, reason);
}

const char *replay_get_filename(void)
{
    return replay_filename;
}
