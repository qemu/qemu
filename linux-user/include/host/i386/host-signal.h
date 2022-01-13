/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef I386_HOST_SIGNAL_H
#define I386_HOST_SIGNAL_H

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.gregs[REG_EIP];
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.gregs[REG_EIP] = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    return uc->uc_mcontext.gregs[REG_TRAPNO] == 0xe
        && (uc->uc_mcontext.gregs[REG_ERR] & 0x2);
}

#endif
