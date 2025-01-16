/*
 * Syscall implementations for semihosting.
 *
 * Copyright (c) 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOSTING_SYSCALLS_H
#define SEMIHOSTING_SYSCALLS_H

#include "exec/cpu-defs.h"
#include "gdbstub/syscalls.h"

/*
 * Argument loading from the guest is performed by the caller;
 * results are returned via the 'complete' callback.
 *
 * String operands are in address/len pairs.  The len argument may be 0
 * (when the semihosting abi does not already provide the length),
 * or non-zero (where it should include the terminating zero).
 */

typedef struct GuestFD GuestFD;

void semihost_sys_open(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       int gdb_flags, int mode);

void semihost_sys_close(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd);

void semihost_sys_read(CPUState *cs, gdb_syscall_complete_cb complete,
                       int fd, target_ulong buf, target_ulong len);

void semihost_sys_read_gf(CPUState *cs, gdb_syscall_complete_cb complete,
                          GuestFD *gf, target_ulong buf, target_ulong len);

void semihost_sys_write(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, target_ulong buf, target_ulong len);

void semihost_sys_write_gf(CPUState *cs, gdb_syscall_complete_cb complete,
                           GuestFD *gf, target_ulong buf, target_ulong len);

void semihost_sys_lseek(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, int64_t off, int gdb_whence);

void semihost_sys_isatty(CPUState *cs, gdb_syscall_complete_cb complete,
                         int fd);

void semihost_sys_flen(CPUState *cs, gdb_syscall_complete_cb fstat_cb,
                       gdb_syscall_complete_cb flen_cb,
                       int fd, target_ulong fstat_addr);

void semihost_sys_fstat(CPUState *cs, gdb_syscall_complete_cb complete,
                        int fd, target_ulong addr);

void semihost_sys_stat(CPUState *cs, gdb_syscall_complete_cb complete,
                       target_ulong fname, target_ulong fname_len,
                       target_ulong addr);

void semihost_sys_remove(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong fname, target_ulong fname_len);

void semihost_sys_rename(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong oname, target_ulong oname_len,
                         target_ulong nname, target_ulong nname_len);

void semihost_sys_system(CPUState *cs, gdb_syscall_complete_cb complete,
                         target_ulong cmd, target_ulong cmd_len);

void semihost_sys_gettimeofday(CPUState *cs, gdb_syscall_complete_cb complete,
                               target_ulong tv_addr, target_ulong tz_addr);

void semihost_sys_poll_one(CPUState *cs, gdb_syscall_complete_cb complete,
                           int fd, GIOCondition cond, int timeout);

#endif /* SEMIHOSTING_SYSCALLS_H */
