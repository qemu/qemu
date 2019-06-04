/*
 * Semihosting Console
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOST_CONSOLE_H
#define SEMIHOST_CONSOLE_H

/**
 * qemu_semihosting_console_out:
 * @env: CPUArchState
 * @s: host address of guest string
 * @len: length of string or 0 (string is null terminated)
 *
 * Send a guest string to the debug console. This may be the remote
 * gdb session if a softmmu guest is currently being debugged.
 *
 * Returns: number of bytes written.
 */
int qemu_semihosting_console_out(CPUArchState *env, target_ulong s, int len);

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
