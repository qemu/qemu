/*
 * gdbstub user-mode only APIs
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef GDBSTUB_USER_H
#define GDBSTUB_USER_H

#define MAX_SIGINFO_LENGTH 128

/**
 * gdb_handlesig() - yield control to gdb
 * @cpu: CPU
 * @sig: if non-zero, the signal number which caused us to stop
 * @reason: stop reason for stop reply packet or NULL
 * @siginfo: target-specific siginfo struct
 * @siginfo_len: target-specific siginfo struct length
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
int gdb_handlesig(CPUState *, int, const char *, void *, int);

/**
 * gdb_signalled() - inform remote gdb of sig exit
 * @as: current CPUArchState
 * @sig: signal number
 */
void gdb_signalled(CPUArchState *as, int sig);

/**
 * gdbserver_fork_start() - inform gdb of the upcoming fork()
 */
void gdbserver_fork_start(void);

/**
 * gdbserver_fork_end() - inform gdb of the completed fork()
 * @cs: CPU
 * @pid: 0 if in child process, -1 if fork failed, child process pid otherwise
 */
void gdbserver_fork_end(CPUState *cs, pid_t pid);

/**
 * gdb_syscall_entry() - inform gdb of syscall entry and yield control to it
 * @cs: CPU
 * @num: syscall number
 */
void gdb_syscall_entry(CPUState *cs, int num);

/**
 * gdb_syscall_entry() - inform gdb of syscall return and yield control to it
 * @cs: CPU
 * @num: syscall number
 */
void gdb_syscall_return(CPUState *cs, int num);

#endif /* GDBSTUB_USER_H */
