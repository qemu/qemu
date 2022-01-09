/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2021 Warner Losh
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef X86_64_HOST_SIGNAL_H
#define X86_64_HOST_SIGNAL_H

#include <sys/ucontext.h>
#include <machine/trap.h>
#include <vm/pmap.h>
#include <machine/pmap.h>

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.mc_rip;
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.mc_rip = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    /*
     * Look in sys/amd64/amd64/trap.c. NOTE: mc_err == tr_err due to type
     * punning between a trapframe and mcontext on FreeBSD/amd64.
     */
    return uc->uc_mcontext.mc_trapno == T_PAGEFLT &&
        uc->uc_mcontext.mc_err & PGEX_W;
}

#endif
