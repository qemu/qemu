/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ALPHA_HOST_SIGNAL_H
#define ALPHA_HOST_SIGNAL_H

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.sc_pc;
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.sc_pc = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    uint32_t *pc = (uint32_t *)host_signal_pc(uc);
    uint32_t insn = *pc;

    /* XXX: need kernel patch to get write flag faster */
    switch (insn >> 26) {
    case 0x0d: /* stw */
    case 0x0e: /* stb */
    case 0x0f: /* stq_u */
    case 0x24: /* stf */
    case 0x25: /* stg */
    case 0x26: /* sts */
    case 0x27: /* stt */
    case 0x2c: /* stl */
    case 0x2d: /* stq */
    case 0x2e: /* stl_c */
    case 0x2f: /* stq_c */
        return true;
    }
    return false;
}

#endif
