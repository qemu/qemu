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

#include <stdbool.h>
#include <stdint.h>
#include "qapi-types.h"

extern ReplayMode replay_mode;

/* Processing the instructions */

/*! Returns number of executed instructions. */
uint64_t replay_get_current_step(void);

#endif
