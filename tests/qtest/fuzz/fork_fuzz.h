/*
 * Fork-based fuzzing helpers
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef FORK_FUZZ_H
#define FORK_FUZZ_H

extern uint8_t __FUZZ_COUNTERS_START;
extern uint8_t __FUZZ_COUNTERS_END;

void counter_shm_init(void);

#endif

