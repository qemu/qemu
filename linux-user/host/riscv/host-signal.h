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

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.__gregs[REG_PC];
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    uint32_t insn = *(uint32_t *)host_signal_pc(uc);

    /*
     * Detect store by reading the instruction at the program
     * counter. Note: we currently only generate 32-bit
     * instructions so we thus only detect 32-bit stores
     */
    switch (((insn >> 0) & 0b11)) {
    case 3:
        switch (((insn >> 2) & 0b11111)) {
        case 8:
            switch (((insn >> 12) & 0b111)) {
            case 0: /* sb */
            case 1: /* sh */
            case 2: /* sw */
            case 3: /* sd */
            case 4: /* sq */
                return true;
            default:
                break;
            }
            break;
        case 9:
            switch (((insn >> 12) & 0b111)) {
            case 2: /* fsw */
            case 3: /* fsd */
            case 4: /* fsq */
                return true;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    /* Check for compressed instructions */
    switch (((insn >> 13) & 0b111)) {
    case 7:
        switch (insn & 0b11) {
        case 0: /*c.sd */
        case 2: /* c.sdsp */
            return true;
        default:
            break;
        }
        break;
    case 6:
        switch (insn & 0b11) {
        case 0: /* c.sw */
        case 3: /* c.swsp */
            return true;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return false;
}

#endif
