/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_TARGET_SYSCALL_H
#define LOONGARCH_TARGET_SYSCALL_H

#include "qemu/units.h"

/*
 * this struct defines the way the registers are stored on the
 * stack during a system call.
 */

struct target_pt_regs {
    /* Saved main processor registers. */
    target_ulong regs[32];

    /* Saved special registers. */
    struct {
        target_ulong era;
        target_ulong badv;
        target_ulong crmd;
        target_ulong prmd;
        target_ulong euen;
        target_ulong ecfg;
        target_ulong estat;
    } csr;
    target_ulong orig_a0;
    target_ulong __last[0];
};

#define UNAME_MACHINE "loongarch64"
#define UNAME_MINIMUM_RELEASE "5.19.0"

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPULoongArchState *env)
{
    return 64 * KiB;
}

#endif
