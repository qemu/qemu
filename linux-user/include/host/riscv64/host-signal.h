/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef RISCV_HOST_SIGNAL_H
#define RISCV_HOST_SIGNAL_H

/* The third argument to a SA_SIGINFO handler is ucontext_t. */
typedef ucontext_t host_sigcontext;

static inline uintptr_t host_signal_pc(host_sigcontext *uc)
{
    return uc->uc_mcontext.__gregs[REG_PC];
}

static inline void host_signal_set_pc(host_sigcontext *uc, uintptr_t pc)
{
    uc->uc_mcontext.__gregs[REG_PC] = pc;
}

static inline void *host_signal_mask(host_sigcontext *uc)
{
    return &uc->uc_sigmask;
}

static inline bool host_signal_write(siginfo_t *info, host_sigcontext *uc)
{
    /*
     * Detect store by reading the instruction at the program counter.
     * Do not read more than 16 bits, because we have not yet determined
     * the size of the instruction.
     */
    const uint16_t *pinsn = (const uint16_t *)host_signal_pc(uc);
    uint16_t insn = pinsn[0];

    /* 16-bit instructions */
    switch (insn & 0xe003) {
    case 0xa000: /* c.fsd */
    case 0xc000: /* c.sw */
    case 0xe000: /* c.sd (rv64) / c.fsw (rv32) */
    case 0xa002: /* c.fsdsp */
    case 0xc002: /* c.swsp */
    case 0xe002: /* c.sdsp (rv64) / c.fswsp (rv32) */
        return true;
    }

    /* 32-bit instructions, major opcodes */
    switch (insn & 0x7f) {
    case 0x23: /* store */
    case 0x27: /* store-fp */
        return true;
    case 0x2f: /* amo */
        /*
         * The AMO function code is in bits 25-31, unread as yet.
         * The AMO functions are LR (read), SC (write), and the
         * rest are all read-modify-write.
         */
        insn = pinsn[1];
        return (insn >> 11) != 2; /* LR */
    }

    return false;
}

#endif
