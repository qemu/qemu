/*
 * Semihosting Console
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOST_CONSOLE_H
#define SEMIHOST_CONSOLE_H

#include "cpu.h"

/**
 * qemu_semihosting_console_outs:
 * @env: CPUArchState
 * @s: host address of null terminated guest string
 *
 * Send a null terminated guest string to the debug console. This may
 * be the remote gdb session if a softmmu guest is currently being
 * debugged.
 *
 * Returns: number of bytes written.
 */
int qemu_semihosting_console_outs(CPUArchState *env, target_ulong s);

/**
 * qemu_semihosting_console_outc:
 * @env: CPUArchState
 * @s: host address of null terminated guest string
 *
 * Send single character from guest memory to the debug console. This
 * may be the remote gdb session if a softmmu guest is currently being
 * debugged.
 *
 * Returns: nothing
 */
void qemu_semihosting_console_outc(CPUArchState *env, target_ulong c);

/**
 * qemu_semihosting_console_inc:
 * @env: CPUArchState
 *
 * Receive single character from debug console. This may be the remote
 * gdb session if a softmmu guest is currently being debugged. As this
 * call may block if no data is available we suspend the CPU and will
 * re-execute the instruction when data is there. Therefore two
 * conditions must be met:
 *   - CPUState is synchronized before calling this function
 *   - pc is only updated once the character is successfully returned
 *
 * Returns: character read OR cpu_loop_exit!
 */
target_ulong qemu_semihosting_console_inc(CPUArchState *env);

/**
 * qemu_semihosting_log_out:
 * @s: pointer to string
 * @len: length of string
 *
 * Send a string to the debug output. Unlike console_out these strings
 * can't be sent to a remote gdb instance as they don't exist in guest
 * memory.
 *
 * Returns: number of bytes written
 */
int qemu_semihosting_log_out(const char *s, int len);

#endif /* SEMIHOST_CONSOLE_H */
