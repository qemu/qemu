/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef XTENSA_TARGET_PTRACE_H
#define XTENSA_TARGET_PTRACE_H

/* See arch/xtensa/include/uapi/asm/ptrace.h. */
struct target_user_pt_regs {
    uint32_t pc;
    uint32_t ps;
    uint32_t lbeg;
    uint32_t lend;
    uint32_t lcount;
    uint32_t sar;
    uint32_t windowstart;
    uint32_t windowbase;
    uint32_t threadptr;
    uint32_t syscall;
    uint32_t reserved[6 + 48];
    uint32_t a[64];
};

#endif /* XTENSA_TARGET_PTRACE_H */
