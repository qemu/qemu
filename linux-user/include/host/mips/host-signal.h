/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef MIPS_HOST_SIGNAL_H
#define MIPS_HOST_SIGNAL_H

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.pc;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.pc = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

#if defined(__misp16) || defined(__mips_micromips)
#error "Unsupported encoding"
#endif

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    uint32_t insn = *(uint32_t *)host_signal_pc(uc);

    /* Detect all store instructions at program counter. */
    switch ((insn >> 26) & 077) {
    case 050: /* SB */
    case 051: /* SH */
    case 052: /* SWL */
    case 053: /* SW */
    case 054: /* SDL */
    case 055: /* SDR */
    case 056: /* SWR */
    case 070: /* SC */
    case 071: /* SWC1 */
    case 074: /* SCD */
    case 075: /* SDC1 */
    case 077: /* SD */
#if !defined(__mips_isa_rev) || __mips_isa_rev < 6
    case 072: /* SWC2 */
    case 076: /* SDC2 */
#endif
        return true;
    case 023: /* COP1X */
        /*
         * Required in all versions of MIPS64 since
         * MIPS64r1 and subsequent versions of MIPS32r2.
         */
        switch (insn & 077) {
        case 010: /* SWXC1 */
        case 011: /* SDXC1 */
        case 015: /* SUXC1 */
            return true;
        }
        break;
    }
    return false;
}

#endif
