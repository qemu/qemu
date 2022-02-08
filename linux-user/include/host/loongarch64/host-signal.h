/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LOONGARCH64_HOST_SIGNAL_H
#define LOONGARCH64_HOST_SIGNAL_H

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.__pc;
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.__pc = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    const uint32_t *pinsn = (const uint32_t *)host_signal_pc(uc);
    uint32_t insn = pinsn[0];

    /* Detect store by reading the instruction at the program counter.  */
    switch ((insn >> 26) & 0b111111) {
    case 0b001000: /* {ll,sc}.[wd] */
        switch ((insn >> 24) & 0b11) {
        case 0b01: /* sc.w */
        case 0b11: /* sc.d */
            return true;
        }
        break;
    case 0b001001: /* {ld,st}ox4.[wd] ({ld,st}ptr.[wd]) */
        switch ((insn >> 24) & 0b11) {
        case 0b01: /* stox4.w (stptr.w) */
        case 0b11: /* stox4.d (stptr.d) */
            return true;
        }
        break;
    case 0b001010: /* {ld,st}.* family */
        switch ((insn >> 22) & 0b1111) {
        case 0b0100: /* st.b */
        case 0b0101: /* st.h */
        case 0b0110: /* st.w */
        case 0b0111: /* st.d */
        case 0b1101: /* fst.s */
        case 0b1111: /* fst.d */
            return true;
        }
        break;
    case 0b001110: /* indexed, atomic, bounds-checking memory operations */
        switch ((insn >> 15) & 0b11111111111) {
        case 0b00000100000: /* stx.b */
        case 0b00000101000: /* stx.h */
        case 0b00000110000: /* stx.w */
        case 0b00000111000: /* stx.d */
        case 0b00001110000: /* fstx.s */
        case 0b00001111000: /* fstx.d */
        case 0b00011101100: /* fstgt.s */
        case 0b00011101101: /* fstgt.d */
        case 0b00011101110: /* fstle.s */
        case 0b00011101111: /* fstle.d */
        case 0b00011111000: /* stgt.b */
        case 0b00011111001: /* stgt.h */
        case 0b00011111010: /* stgt.w */
        case 0b00011111011: /* stgt.d */
        case 0b00011111100: /* stle.b */
        case 0b00011111101: /* stle.h */
        case 0b00011111110: /* stle.w */
        case 0b00011111111: /* stle.d */
        case 0b00011000000 ... 0b00011100011: /* am* insns */
            return true;
        }
        break;
    }

    return false;
}

#endif
