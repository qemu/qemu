/*
 * gdbstub user-mode only APIs
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0+
 */

#ifndef GDBSTUB_USER_H
#define GDBSTUB_USER_H

/**
 * gdb_handlesig() - yield control to gdb
 * @cpu: CPU
 * @sig: if non-zero, the signal number which caused us to stop
 *
 * This function yields control to gdb, when a user-mode-only target
 * needs to stop execution. If @sig is non-zero, then we will send a
 * stop packet to tell gdb that we have stopped because of this signal.
 *
 * This function will block (handling protocol requests from gdb)
 * until gdb tells us to continue target execution. When it does
 * return, the return value is a signal to deliver to the target,
 * or 0 if no signal should be delivered, ie the signal that caused
 * us to stop should be ignored.
 */
int gdb_handlesig(CPUState *, int);

/**
 * gdb_signalled() - inform remote gdb of sig exit
 * @as: current CPUArchState
 * @sig: signal number
 */
void gdb_signalled(CPUArchState *as, int sig);

/**
 * gdbserver_fork() - disable gdb stub for child processes.
 * @cs: CPU
 */
void gdbserver_fork(CPUState *cs);


#endif /* GDBSTUB_USER_H */
