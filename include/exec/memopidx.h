/*
 * Combine the MemOp and mmu_idx parameters into a single value.
 *
 * Authors:
 *  Richard Henderson <rth@twiddle.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC_MEMOPIDX_H
#define EXEC_MEMOPIDX_H

#include "exec/memop.h"

typedef uint32_t MemOpIdx;

/**
 * make_memop_idx
 * @op: memory operation
 * @idx: mmu index
 *
 * Encode these values into a single parameter.
 */
static inline MemOpIdx make_memop_idx(MemOp op, unsigned idx)
{
#ifdef CONFIG_DEBUG_TCG
    assert(idx <= 31);
    assert(clz32(op) >= 5);
#endif
    return (op << 5) | idx;
}

/**
 * get_memop
 * @oi: combined op/idx parameter
 *
 * Extract the memory operation from the combined value.
 */
static inline MemOp get_memop(MemOpIdx oi)
{
    return oi >> 5;
}

/**
 * get_mmuidx
 * @oi: combined op/idx parameter
 *
 * Extract the mmu index from the combined value.
 */
static inline unsigned get_mmuidx(MemOpIdx oi)
{
    return oi & 31;
}

#endif
