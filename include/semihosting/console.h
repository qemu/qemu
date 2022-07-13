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

/*
 * qemu_semihosting_console_block_until_ready:
 * @cs: CPUState
 *
 * If no data is available we suspend the CPU and will re-execute the
 * instruction when data is available.
 */
void qemu_semihosting_console_block_until_ready(CPUState *cs);

/**
 * qemu_semihosting_console_ready:
 *
 * Return true if characters are available for read; does not block.
 */
bool qemu_semihosting_console_ready(void);

#endif /* SEMIHOST_CONSOLE_H */
