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

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.arm_pc;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.arm_pc = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
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
