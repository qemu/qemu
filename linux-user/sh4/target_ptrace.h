/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SH4_TARGET_PTRACE_H
#define SH4_TARGET_PTRACE_H

/* See arch/sh/include/uapi/asm/ptrace_32.h. */
struct target_pt_regs {
    abi_ulong regs[16];
    abi_ulong pc;
    abi_ulong pr;
    abi_ulong sr;
    abi_ulong gbr;
    abi_ulong mach;
    abi_ulong macl;
    abi_long tra;
};

#endif /* SH4_TARGET_PTRACE_H */
