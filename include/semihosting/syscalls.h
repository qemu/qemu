/*
 * Syscall implementations for semihosting.
 *
 * Copyright (c) 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOSTING_SYSCALLS_H
#define SEMIHOSTING_SYSCALLS_H

/*
 * Argument loading from the guest is performed by the caller;
 * results are returned via the 'complete' callback.
 *
 * String operands are in address/len pairs.  The len argument may be 0
 * (when the semihosting abi does not already provide the length),
 * or non-zero (where it should include the terminating zero).
 */

void semihost_sys_open(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       int gdb_flags, int mode);

void semihost_sys_close(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd);

#endif /* SEMIHOSTING_SYSCALLS_H */
