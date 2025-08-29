/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef ARM_TARGET_PTRACE_H
#define ARM_TARGET_PTRACE_H

/*
 * See arch/arm/include/uapi/asm/ptrace.h.
 * Instead of an array and ARM_xx defines, use proper fields.
 */
struct target_pt_regs {
    abi_ulong regs[16];
    abi_ulong cpsr;
    abi_ulong orig_r0;
};

#endif /* ARM_TARGET_PTRACE_H */
