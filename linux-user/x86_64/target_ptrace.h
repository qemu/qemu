/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef X86_64_TARGET_PTRACE_H
#define X86_64_TARGET_PTRACE_H

/*
 * The struct pt_regs in arch/x86/include/uapi/asm/ptrace.h has missing
 * register values and is not used.  See arch/x86/include/asm/user_64.h.
 */
struct target_user_regs_struct {
    abi_ulong r15;
    abi_ulong r14;
    abi_ulong r13;
    abi_ulong r12;
    abi_ulong bp;
    abi_ulong bx;
    abi_ulong r11;
    abi_ulong r10;
    abi_ulong r9;
    abi_ulong r8;
    abi_ulong ax;
    abi_ulong cx;
    abi_ulong dx;
    abi_ulong si;
    abi_ulong di;
    abi_ulong orig_ax;
    abi_ulong ip;
    abi_ulong cs;
    abi_ulong flags;
    abi_ulong sp;
    abi_ulong ss;
    abi_ulong fs_base;
    abi_ulong gs_base;
    abi_ulong ds;
    abi_ulong es;
    abi_ulong fs;
    abi_ulong gs;
};

#endif /* X86_64_TARGET_PTRACE_H */
