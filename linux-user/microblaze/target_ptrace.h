/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MICROBLAZE_TARGET_PTRACE_H
#define MICROBLAZE_TARGET_PTRACE_H

/* We use microblaze_reg_t to keep things similar to the kernel sources.  */
typedef uint32_t microblaze_reg_t;

struct target_pt_regs {
    /* Note the kernel enumerates all 32 registers. */
    microblaze_reg_t r[32];
    microblaze_reg_t pc;
    microblaze_reg_t msr;
    microblaze_reg_t ear;
    microblaze_reg_t esr;
    microblaze_reg_t fsr;
    uint32_t kernel_mode;
};

#endif /* MICROBLAZE_TARGET_PTRACE_H */
