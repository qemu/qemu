/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef S390_HOST_SIGNAL_H
#define S390_HOST_SIGNAL_H

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.psw.addr;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.psw.addr = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    uint16_t *pinsn = (uint16_t *)host_signal_pc(uc);

    /*
     * ??? On linux, the non-rt signal handler has 4 (!) arguments instead
     * of the normal 2 arguments.  The 4th argument contains the "Translation-
     * Exception Identification for DAT Exceptions" from the hardware (aka
     * "int_parm_long"), which does in fact contain the is_write value.
     * The rt signal handler, as far as I can tell, does not give this value
     * at all.  Not that we could get to it from here even if it were.
     * So fall back to parsing instructions.  Treat read-modify-write ones as
     * writes, which is not fully correct, but for tracking self-modifying code
     * this is better than treating them as reads.  Checking si_addr page flags
     * might be a viable improvement, albeit a racy one.
     */
    /* ??? This is not even close to complete.  */
    switch (pinsn[0] >> 8) {
    case 0x50: /* ST */
    case 0x42: /* STC */
    case 0x40: /* STH */
    case 0xba: /* CS */
    case 0xbb: /* CDS */
        return true;
    case 0xc4: /* RIL format insns */
        switch (pinsn[0] & 0xf) {
        case 0xf: /* STRL */
        case 0xb: /* STGRL */
        case 0x7: /* STHRL */
            return true;
        }
        break;
    case 0xc8: /* SSF format insns */
        switch (pinsn[0] & 0xf) {
        case 0x2: /* CSST */
            return true;
        }
        break;
    case 0xe3: /* RXY format insns */
        switch (pinsn[2] & 0xff) {
        case 0x50: /* STY */
        case 0x24: /* STG */
        case 0x72: /* STCY */
        case 0x70: /* STHY */
        case 0x8e: /* STPQ */
        case 0x3f: /* STRVH */
        case 0x3e: /* STRV */
        case 0x2f: /* STRVG */
            return true;
        }
        break;
    case 0xeb: /* RSY format insns */
        switch (pinsn[2] & 0xff) {
        case 0x14: /* CSY */
        case 0x30: /* CSG */
        case 0x31: /* CDSY */
        case 0x3e: /* CDSG */
        case 0xe4: /* LANG */
        case 0xe6: /* LAOG */
        case 0xe7: /* LAXG */
        case 0xe8: /* LAAG */
        case 0xea: /* LAALG */
        case 0xf4: /* LAN */
        case 0xf6: /* LAO */
        case 0xf7: /* LAX */
        case 0xfa: /* LAAL */
        case 0xf8: /* LAA */
            return true;
        }
        break;
    }
    return false;
}

#endif
