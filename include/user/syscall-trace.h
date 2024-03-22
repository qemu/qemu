/*
 * Common System Call Tracing Wrappers for *-user
 *
 * Copyright (c) 2019 Linaro
 * Written by Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SYSCALL_TRACE_H
#define SYSCALL_TRACE_H

#include "user/abitypes.h"
#include "gdbstub/user.h"
#include "qemu/plugin.h"
#include "trace/trace-root.h"

/*
 * These helpers just provide a common place for the various
 * subsystems that want to track syscalls to put their hooks in. We
 * could potentially unify the -strace code here as well.
 */

static inline void record_syscall_start(CPUState *cpu, int num,
                                        abi_long arg1, abi_long arg2,
                                        abi_long arg3, abi_long arg4,
                                        abi_long arg5, abi_long arg6,
                                        abi_long arg7, abi_long arg8)
{
    qemu_plugin_vcpu_syscall(cpu, num,
                             arg1, arg2, arg3, arg4,
                             arg5, arg6, arg7, arg8);
    gdb_syscall_entry(cpu, num);
}

static inline void record_syscall_return(CPUState *cpu, int num, abi_long ret)
{
    qemu_plugin_vcpu_syscall_ret(cpu, num, ret);
    gdb_syscall_return(cpu, num);
}


#endif /* SYSCALL_TRACE_H */
