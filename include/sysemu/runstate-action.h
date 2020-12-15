/*
 * Copyright (c) 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef RUNSTATE_ACTION_H
#define RUNSTATE_ACTION_H

#include "qapi/qapi-commands-run-state.h"

/* in softmmu/runstate-action.c */
extern RebootAction reboot_action;
extern ShutdownAction shutdown_action;
extern PanicAction panic_action;

#endif /* RUNSTATE_ACTION_H */
