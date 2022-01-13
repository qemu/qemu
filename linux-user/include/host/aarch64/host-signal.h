/*
 * host-signal.h: signal info dependent on the host architecture
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2021 Linaro Limited
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef AARCH64_HOST_SIGNAL_H
#define AARCH64_HOST_SIGNAL_H

/* Pre-3.16 kernel headers don't have these, so provide fallback definitions */
#ifndef ESR_MAGIC
#define ESR_MAGIC 0x45535201
struct esr_context {
    struct _aarch64_ctx head;
    uint64_t esr;
};
#endif

static inline struct _aarch64_ctx *first_ctx(ucontext_t *uc)
{
    return (struct _aarch64_ctx *)&uc->uc_mcontext.__reserved;
}

static inline struct _aarch64_ctx *next_ctx(struct _aarch64_ctx *hdr)
{
    return (struct _aarch64_ctx *)((char *)hdr + hdr->size);
}

static inline uintptr_t host_signal_pc(ucontext_t *uc)
{
    return uc->uc_mcontext.pc;
}

static inline void host_signal_set_pc(ucontext_t *uc, uintptr_t pc)
{
    uc->uc_mcontext.pc = pc;
}

static inline bool host_signal_write(siginfo_t *info, ucontext_t *uc)
{
    struct _aarch64_ctx *hdr;
    uint32_t insn;

    /* Find the esr_context, which has the WnR bit in it */
    for (hdr = first_ctx(uc); hdr->magic; hdr = next_ctx(hdr)) {
        if (hdr->magic == ESR_MAGIC) {
            struct esr_context const *ec = (struct esr_context const *)hdr;
            uint64_t esr = ec->esr;

            /* For data aborts ESR.EC is 0b10010x: then bit 6 is the WnR bit */
            return extract32(esr, 27, 5) == 0x12 && extract32(esr, 6, 1) == 1;
        }
    }

    /*
     * Fall back to parsing instructions; will only be needed
     * for really ancient (pre-3.16) kernels.
     */
    insn = *(uint32_t *)host_signal_pc(uc);

    return (insn & 0xbfff0000) == 0x0c000000   /* C3.3.1 */
        || (insn & 0xbfe00000) == 0x0c800000   /* C3.3.2 */
        || (insn & 0xbfdf0000) == 0x0d000000   /* C3.3.3 */
        || (insn & 0xbfc00000) == 0x0d800000   /* C3.3.4 */
        || (insn & 0x3f400000) == 0x08000000   /* C3.3.6 */
        || (insn & 0x3bc00000) == 0x39000000   /* C3.3.13 */
        || (insn & 0x3fc00000) == 0x3d800000   /* ... 128bit */
        /* Ignore bits 10, 11 & 21, controlling indexing.  */
        || (insn & 0x3bc00000) == 0x38000000   /* C3.3.8-12 */
        || (insn & 0x3fe00000) == 0x3c800000   /* ... 128bit */
        /* Ignore bits 23 & 24, controlling indexing.  */
        || (insn & 0x3a400000) == 0x28000000; /* C3.3.7,14-16 */
}

#endif
