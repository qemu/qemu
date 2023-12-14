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

#ifndef QEMU_MIGRATION_GLOBAL_STATE_H
#define QEMU_MIGRATION_GLOBAL_STATE_H

#include "qapi/qapi-types-run-state.h"

void register_global_state(void);
void global_state_store(void);
void global_state_store_running(void);
bool global_state_received(void);
RunState global_state_get_runstate(void);

#endif
