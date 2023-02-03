/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2022 Linaro Ltd.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef PPC_HOST_SIGNAL_H
#define PPC_HOST_SIGNAL_H

#include <asm/ptrace.h>

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.regs->nip;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.regs->nip = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    return uc->uc_mcontext.regs->trap != 0x400
        && (uc->uc_mcontext.regs->dsisr & 0x02000000);
}

#endif
