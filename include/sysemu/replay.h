#ifndef REPLAY_H
#define REPLAY_H

/*
 * replay.h
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qapi-types.h"
#include "qemu/typedefs.h"

/* replay clock kinds */
enum ReplayClockKind {
    /* host_clock */
    REPLAY_CLOCK_HOST,
    /* virtual_rt_clock */
    REPLAY_CLOCK_VIRTUAL_RT,
    REPLAY_CLOCK_COUNT
};
typedef enum ReplayClockKind ReplayClockKind;

/* IDs of the checkpoints */
enum ReplayCheckpoint {
    CHECKPOINT_CLOCK_WARP,
    CHECKPOINT_RESET_REQUESTED,
    CHECKPOINT_SUSPEND_REQUESTED,
    CHECKPOINT_CLOCK_VIRTUAL,
    CHECKPOINT_CLOCK_HOST,
    CHECKPOINT_CLOCK_VIRTUAL_RT,
    CHECKPOINT_INIT,
    CHECKPOINT_RESET,
    CHECKPOINT_COUNT
};
typedef enum ReplayCheckpoint ReplayCheckpoint;

extern ReplayMode replay_mode;

/* Replay process control functions */

/*! Enables recording or saving event log with specified parameters */
void replay_configure(struct QemuOpts *opts);
/*! Initializes timers used for snapshotting and enables events recording */
void replay_start(void);
/*! Closes replay log file and frees other resources. */
void replay_finish(void);
/*! Adds replay blocker with the specified error description */
void replay_add_blocker(Error *reason);

/* Processing the instructions */

/*! Returns number of executed instructions. */
uint64_t replay_get_current_step(void);
/*! Returns number of instructions to execute in replay mode. */
int replay_get_instructions(void);
/*! Updates instructions counter in replay mode. */
void replay_account_executed_instructions(void);

/* Interrupts and exceptions */

/*! Called by exception handler to write or read
    exception processing events. */
bool replay_exception(void);
/*! Used to determine that exception is pending.
    Does not proceed to the next event in the log. */
bool replay_has_exception(void);
/*! Called by interrupt handlers to write or read
    interrupt processing events.
    \return true if interrupt should be processed */
bool replay_interrupt(void);
/*! Tries to read interrupt event from the file.
    Returns true, when interrupt request is pending */
bool replay_has_interrupt(void);

/* Processing clocks and other time sources */

/*! Save the specified clock */
int64_t replay_save_clock(ReplayClockKind kind, int64_t clock);
/*! Read the specified clock from the log or return cached data */
int64_t replay_read_clock(ReplayClockKind kind);
/*! Saves or reads the clock depending on the current replay mode. */
#define REPLAY_CLOCK(clock, value)                                      \
    (replay_mode == REPLAY_MODE_PLAY ? replay_read_clock((clock))       \
        : replay_mode == REPLAY_MODE_RECORD                             \
            ? replay_save_clock((clock), (value))                       \
        : (value))

/* Events */

/*! Called when qemu shutdown is requested. */
void replay_shutdown_request(void);
/*! Should be called at check points in the execution.
    These check points are skipped, if they were not met.
    Saves checkpoint in the SAVE mode and validates in the PLAY mode.
    Returns 0 in PLAY mode if checkpoint was not found.
    Returns 1 in all other cases. */
bool replay_checkpoint(ReplayCheckpoint checkpoint);

/* Asynchronous events queue */

/*! Disables storing events in the queue */
void replay_disable_events(void);
/*! Returns true when saving events is enabled */
bool replay_events_enabled(void);
/*! Adds bottom half event to the queue */
void replay_bh_schedule_event(QEMUBH *bh);
/*! Adds input event to the queue */
void replay_input_event(QemuConsole *src, InputEvent *evt);
/*! Adds input sync event to the queue */
void replay_input_sync_event(void);

#endif
