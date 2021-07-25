/*
 * Helper functions for guest memory tracing
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__MEM_H
#define TRACE__MEM_H

#include "exec/memopidx.h"

#define TRACE_MEM_SZ_SHIFT_MASK 0xf /* size shift mask */
#define TRACE_MEM_SE (1ULL << 4)    /* sign extended (y/n) */
#define TRACE_MEM_BE (1ULL << 5)    /* big endian (y/n) */
#define TRACE_MEM_ST (1ULL << 6)    /* store (y/n) */
#define TRACE_MEM_MMU_SHIFT 8       /* mmu idx */

/**
 * trace_mem_get_info:
 *
 * Return a value for the 'info' argument in guest memory access traces.
 */
static inline uint16_t trace_mem_get_info(MemOpIdx oi, bool store)
{
    MemOp op = get_memop(oi);
    uint32_t size_shift = op & MO_SIZE;
    bool sign_extend = op & MO_SIGN;
    bool big_endian = (op & MO_BSWAP) == MO_BE;
    uint16_t res;

    res = size_shift & TRACE_MEM_SZ_SHIFT_MASK;
    if (sign_extend) {
        res |= TRACE_MEM_SE;
    }
    if (big_endian) {
        res |= TRACE_MEM_BE;
    }
    if (store) {
        res |= TRACE_MEM_ST;
    }
#ifdef CONFIG_SOFTMMU
    res |= get_mmuidx(oi) << TRACE_MEM_MMU_SHIFT;
#endif

    return res;
}

#endif /* TRACE__MEM_H */
