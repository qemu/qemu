/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (C) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef X86_64_HOST_SIGNAL_H
#define X86_64_HOST_SIGNAL_H

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.gregs[REG_RIP];
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.gregs[REG_RIP] = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    return uc->uc_mcontext.gregs[REG_TRAPNO] == 0xe
        && (uc->uc_mcontext.gregs[REG_ERR] & 0x2);
}

#endif
