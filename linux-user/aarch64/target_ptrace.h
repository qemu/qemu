/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef AARCH64_TARGET_PTRACE_H
#define AARCH64_TARGET_PTRACE_H

/* See arch/arm64/include/uapi/asm/ptrace.h. */
struct target_user_pt_regs {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

#endif /* AARCH64_TARGET_PTRACE_H */
