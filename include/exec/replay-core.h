/*
 * QEMU replay core API
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_REPLAY_H
#define EXEC_REPLAY_H

#include "qapi/qapi-types-replay.h"

extern ReplayMode replay_mode;

/* Replay process control functions */

/* Enables recording or saving event log with specified parameters */
void replay_configure(struct QemuOpts *opts);
/* Initializes timers used for snapshotting and enables events recording */
void replay_start(void);
/* Closes replay log file and frees other resources. */
void replay_finish(void);
/* Adds replay blocker with the specified error description */
void replay_add_blocker(const char *feature);
/* Returns name of the replay log file */
const char *replay_get_filename(void);

/*
 * Start making one step in backward direction.
 * Used by gdbstub for backwards debugging.
 * Returns true on success.
 */
bool replay_reverse_step(void);
/*
 * Start searching the last breakpoint/watchpoint.
 * Used by gdbstub for backwards debugging.
 * Returns true if the process successfully started.
 */
bool replay_reverse_continue(void);
/*
 * Returns true if replay module is processing
 * reverse_continue or reverse_step request
 */
bool replay_running_debug(void);
/* Called in reverse debugging mode to collect breakpoint information */
void replay_breakpoint(void);
/* Called when gdb is attached to gdbstub */
void replay_gdb_attached(void);

/* Interrupts and exceptions */

/* Called by exception handler to write or read exception processing events */
bool replay_exception(void);
/*
 * Used to determine that exception is pending.
 * Does not proceed to the next event in the log.
 */
bool replay_has_exception(void);
/*
 * Called by interrupt handlers to write or read interrupt processing events.
 * Returns true if interrupt should be processed.
 */
bool replay_interrupt(void);
/*
 * Tries to read interrupt event from the file.
 * Returns true, when interrupt request is pending.
 */
bool replay_has_interrupt(void);

/* Processing data from random generators */

/* Saves the values from the random number generator */
void replay_save_random(int ret, void *buf, size_t len);
/* Loads the saved values for the random number generator */
int replay_read_random(void *buf, size_t len);

#endif
