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

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.gregs[REG_RIP];
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.gregs[REG_RIP] = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    return uc->uc_mcontext.gregs[REG_TRAPNO] == 0xe
        && (uc->uc_mcontext.gregs[REG_ERR] & 0x2);
}

#endif
