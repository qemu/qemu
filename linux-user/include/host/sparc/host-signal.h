/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SPARC_HOST_SIGNAL_H
#define SPARC_HOST_SIGNAL_H

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
#ifdef __arch64__
    return uc->uc_mcontext.mc_gregs[MC_PC];
#else
    return uc->uc_mcontext.gregs[REG_PC];
#endif
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
#ifdef __arch64__
    uc->uc_mcontext.mc_gregs[MC_PC] = pc;
#else
    uc->uc_mcontext.gregs[REG_PC] = pc;
#endif
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    uint32_t insn = *(uint32_t *)host_signal_pc(uc);

    if ((insn >> 30) == 3) {
        switch ((insn >> 19) & 0x3f) {
        case 0x05: /* stb */
        case 0x15: /* stba */
        case 0x06: /* sth */
        case 0x16: /* stha */
        case 0x04: /* st */
        case 0x14: /* sta */
        case 0x07: /* std */
        case 0x17: /* stda */
        case 0x0e: /* stx */
        case 0x1e: /* stxa */
        case 0x24: /* stf */
        case 0x34: /* stfa */
        case 0x27: /* stdf */
        case 0x37: /* stdfa */
        case 0x26: /* stqf */
        case 0x36: /* stqfa */
        case 0x25: /* stfsr */
        case 0x3c: /* casa */
        case 0x3e: /* casxa */
            return true;
        }
    }
    return false;
}

#endif
