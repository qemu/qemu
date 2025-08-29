/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef SPARC_TARGET_PTRACE_H
#define SPARC_TARGET_PTRACE_H

/* See arch/sparc/include/uapi/asm/ptrace.h. */
struct target_pt_regs {
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    abi_ulong u_regs[16];
    abi_ulong tstate;
    abi_ulong pc;
    abi_ulong npc;
    uint32_t y;
    uint32_t magic;
#else
    abi_ulong psr;
    abi_ulong pc;
    abi_ulong npc;
    abi_ulong y;
    abi_ulong u_regs[16];
#endif
};

#endif /* SPARC_TARGET_PTRACE_H */
