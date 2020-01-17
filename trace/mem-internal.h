/*
 * Helper functions for guest memory tracing
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__MEM_INTERNAL_H
#define TRACE__MEM_INTERNAL_H

#define TRACE_MEM_SZ_SHIFT_MASK 0xf /* size shift mask */
#define TRACE_MEM_SE (1ULL << 4)    /* sign extended (y/n) */
#define TRACE_MEM_BE (1ULL << 5)    /* big endian (y/n) */
#define TRACE_MEM_ST (1ULL << 6)    /* store (y/n) */
#define TRACE_MEM_MMU_SHIFT 8       /* mmu idx */

static inline uint16_t trace_mem_build_info(
    int size_shift, bool sign_extend, MemOp endianness,
    bool store, unsigned int mmu_idx)
{
    uint16_t res;

    res = size_shift & TRACE_MEM_SZ_SHIFT_MASK;
    if (sign_extend) {
        res |= TRACE_MEM_SE;
    }
    if (endianness == MO_BE) {
        res |= TRACE_MEM_BE;
    }
    if (store) {
        res |= TRACE_MEM_ST;
    }
#ifdef CONFIG_SOFTMMU
    res |= mmu_idx << TRACE_MEM_MMU_SHIFT;
#endif
    return res;
}

static inline uint16_t trace_mem_get_info(MemOp op,
                                          unsigned int mmu_idx,
                                          bool store)
{
    return trace_mem_build_info(op & MO_SIZE, !!(op & MO_SIGN),
                                op & MO_BSWAP, store,
                                mmu_idx);
}

#endif /* TRACE__MEM_INTERNAL_H */
