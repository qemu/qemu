/*
 * QEMU replay (system interface)
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef SYSTEM_REPLAY_H
#define SYSTEM_REPLAY_H

#include "exec/replay-core.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-types-run-state.h"
#include "qapi/qapi-types-ui.h"
#include "block/aio.h"

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
    CHECKPOINT_CLOCK_WARP_START,
    CHECKPOINT_CLOCK_WARP_ACCOUNT,
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

typedef struct ReplayNetState ReplayNetState;

/* Name of the initial VM snapshot */
extern char *replay_snapshot;

/* Replay locking
 *
 * The locks are needed to protect the shared structures and log file
 * when doing record/replay. They also are the main sync-point between
 * the main-loop thread and the vCPU thread. This was a role
 * previously filled by the BQL which has been busy trying to reduce
 * its impact across the code. This ensures blocks of events stay
 * sequential and reproducible.
 */

void replay_mutex_lock(void);
void replay_mutex_unlock(void);

/* Processing the instructions */

/*! Returns number of executed instructions. */
uint64_t replay_get_current_icount(void);
/*! Returns number of instructions to execute in replay mode. */
int replay_get_instructions(void);
/*! Updates instructions counter in replay mode. */
void replay_account_executed_instructions(void);

/* Processing clocks and other time sources */

/*! Save the specified clock */
int64_t replay_save_clock(ReplayClockKind kind, int64_t clock,
                          int64_t raw_icount);
/*! Read the specified clock from the log or return cached data */
int64_t replay_read_clock(ReplayClockKind kind, int64_t raw_icount);
/*! Saves or reads the clock depending on the current replay mode. */
#define REPLAY_CLOCK(clock, value)                                      \
    !icount_enabled() ? (value) :                                       \
    (replay_mode == REPLAY_MODE_PLAY                                    \
        ? replay_read_clock((clock), icount_get_raw())                  \
        : replay_mode == REPLAY_MODE_RECORD                             \
            ? replay_save_clock((clock), (value), icount_get_raw())     \
            : (value))
#define REPLAY_CLOCK_LOCKED(clock, value)                               \
    !icount_enabled() ? (value) :                                       \
    (replay_mode == REPLAY_MODE_PLAY                                    \
        ? replay_read_clock((clock), icount_get_raw_locked())           \
        : replay_mode == REPLAY_MODE_RECORD                             \
            ? replay_save_clock((clock), (value), icount_get_raw_locked()) \
            : (value))

/* Events */

/*! Called when qemu shutdown is requested. */
void replay_shutdown_request(ShutdownCause cause);
/*! Should be called at check points in the execution.
    These check points are skipped, if they were not met.
    Saves checkpoint in the SAVE mode and validates in the PLAY mode.
    Returns 0 in PLAY mode if checkpoint was not found.
    Returns 1 in all other cases. */
bool replay_checkpoint(ReplayCheckpoint checkpoint);
/*! Used to determine that checkpoint or async event is pending.
    Does not proceed to the next event in the log. */
bool replay_has_event(void);
/*
 * Processes the async events added to the queue (while recording)
 * or reads the events from the file (while replaying).
 */
void replay_async_events(void);

/* Asynchronous events queue */

/*! Enables storing events in the queue */
void replay_enable_events(void);
/*! Returns true when saving events is enabled */
bool replay_events_enabled(void);
/* Flushes events queue */
void replay_flush_events(void);
/*! Adds bottom half event to the queue */
void replay_bh_schedule_event(QEMUBH *bh);
/* Adds oneshot bottom half event to the queue */
void replay_bh_schedule_oneshot_event(AioContext *ctx,
    QEMUBHFunc *cb, void *opaque);
/*! Adds input event to the queue */
void replay_input_event(QemuConsole *src, InputEvent *evt);
/*! Adds input sync event to the queue */
void replay_input_sync_event(void);
/*! Adds block layer event to the queue */
void replay_block_event(QEMUBH *bh, uint64_t id);
/*! Returns ID for the next block event */
uint64_t blkreplay_next_id(void);

/* Character device */

/*! Registers char driver to save it's events */
void replay_register_char_driver(struct Chardev *chr);
/*! Saves write to char device event to the log */
void replay_chr_be_write(struct Chardev *s, const uint8_t *buf, int len);
/*! Writes char write return value to the replay log. */
void replay_char_write_event_save(int res, int offset);
/*! Reads char write return value from the replay log. */
void replay_char_write_event_load(int *res, int *offset);
/*! Reads information about read_all character event. */
int replay_char_read_all_load(uint8_t *buf);
/*! Writes character read_all error code into the replay log. */
void replay_char_read_all_save_error(int res);
/*! Writes character read_all execution result into the replay log. */
void replay_char_read_all_save_buf(uint8_t *buf, int offset);

/* Network */

/*! Registers replay network filter attached to some backend. */
ReplayNetState *replay_register_net(NetFilterState *nfs);
/*! Unregisters replay network filter. */
void replay_unregister_net(ReplayNetState *rns);
/*! Called to write network packet to the replay log. */
void replay_net_packet_event(ReplayNetState *rns, unsigned flags,
                             const struct iovec *iov, int iovcnt);

/* Audio */

/*! Saves/restores number of played samples of audio out operation. */
void replay_audio_out(size_t *played);
/*! Saves/restores recorded samples of audio in operation. */
void replay_audio_in(size_t *recorded, void *samples, size_t *wpos, size_t size);

/* VM state operations */

/*! Called at the start of execution.
    Loads or saves initial vmstate depending on execution mode. */
void replay_vmstate_init(void);
/*! Called to ensure that replay state is consistent and VM snapshot
    can be created */
bool replay_can_snapshot(void);

#endif
