/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MIPS_TARGET_PTRACE_H
#define MIPS_TARGET_PTRACE_H

struct target_pt_regs {
    abi_ulong pad0[6];
    abi_ulong regs[32];
    abi_ulong lo;
    abi_ulong hi;
    abi_ulong cp0_epc;
    abi_ulong cp0_badvaddr;
    abi_ulong cp0_status;
    abi_ulong cp0_cause;
};

#endif /* MIPS_TARGET_PTRACE_H */
