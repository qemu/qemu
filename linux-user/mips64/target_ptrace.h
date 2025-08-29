/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MIPS64_TARGET_PTRACE_H
#define MIPS64_TARGET_PTRACE_H

struct target_pt_regs {
    target_ulong regs[32];
    target_ulong lo;
    target_ulong hi;
    target_ulong cp0_epc;
    target_ulong cp0_badvaddr;
    target_ulong cp0_status;
    target_ulong cp0_cause;
};

#endif /* MIPS64_TARGET_PTRACE_H */
