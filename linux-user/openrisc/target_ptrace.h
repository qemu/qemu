/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef OPENRISC_TARGET_PTRACE_H
#define OPENRISC_TARGET_PTRACE_H

/* See arch/openrisc/include/uapi/asm/ptrace.h. */
struct target_user_regs_struct {
    abi_ulong gpr[32];
    abi_ulong pc;
    abi_ulong sr;
};

#endif /* OPENRISC_TARGET_PTRACE_H */
