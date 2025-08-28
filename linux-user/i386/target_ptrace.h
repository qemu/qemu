/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef I386_TARGET_PTRACE_H
#define I386_TARGET_PTRACE_H

/*
 * Note that arch/x86/include/uapi/asm/ptrace.h (struct pt_regs) and
 * arch/x86/include/asm/user_32.h (struct user_regs_struct) have the
 * same layout, though the exact types differ (int vs long vs unsigned).
 * Define user_regs_struct because that's what's actually used.
 */
struct target_user_regs_struct {
    abi_ulong bx;
    abi_ulong cx;
    abi_ulong dx;
    abi_ulong si;
    abi_ulong di;
    abi_ulong bp;
    abi_ulong ax;
    abi_ulong ds;
    abi_ulong es;
    abi_ulong fs;
    abi_ulong gs;
    abi_ulong orig_ax;
    abi_ulong ip;
    abi_ulong cs;
    abi_ulong flags;
    abi_ulong sp;
    abi_ulong ss;
};

#endif /* I386_TARGET_PTRACE_H */
