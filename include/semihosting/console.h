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
 * qemu_semihosting_console_read:
 * @cs: CPUState
 * @buf: host buffer
 * @len: buffer size
 *
 * Receive at least one character from debug console.  As this call may
 * block if no data is available we suspend the CPU and will re-execute the
 * instruction when data is there. Therefore two conditions must be met:
 *
 *   - CPUState is synchronized before calling this function
 *   - pc is only updated once the character is successfully returned
 *
 * Returns: number of characters read, OR cpu_loop_exit!
 */
int qemu_semihosting_console_read(CPUState *cs, void *buf, int len);

/**
 * qemu_semihosting_console_write:
 * @buf: host buffer
 * @len: buffer size
 *
 * Write len bytes from buf to the debug console.
 *
 * Returns: number of bytes written -- this should only ever be short
 * on some sort of i/o error.
 */
int qemu_semihosting_console_write(void *buf, int len);

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
