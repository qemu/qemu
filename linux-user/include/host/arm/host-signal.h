/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ARM_HOST_SIGNAL_H
#define ARM_HOST_SIGNAL_H

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.arm_pc;
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.arm_pc = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    /*
     * In the FSR, bit 11 is WnR, assuming a v6 or
     * later processor.  On v5 we will always report
     * this as a read, which will fail later.
     */
    uint32_t fsr = uc->uc_mcontext.error_code;
    return extract32(fsr, 11, 1);
}

#endif
