/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef PPC_HOST_SIGNAL_H
#define PPC_HOST_SIGNAL_H

/* Needed for PT_* constants */
#include <asm/ptrace.h>

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.gp_regs[PT_NIP];
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.gp_regs[PT_NIP] = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    return uc->uc_mcontext.gp_regs[PT_TRAP] != 0x400
        && (uc->uc_mcontext.gp_regs[PT_DSISR] & 0x02000000);
}

#endif
