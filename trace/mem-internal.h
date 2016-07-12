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

static inline uint8_t trace_mem_get_info(TCGMemOp op, bool store)
{
    uint8_t res = op;
    bool be = (op & MO_BSWAP) == MO_BE;

    /* remove untraced fields */
    res &= (1ULL << 4) - 1;
    /* make endianness absolute */
    res &= ~MO_BSWAP;
    if (be) {
        res |= 1ULL << 3;
    }
    /* add fields */
    if (store) {
        res |= 1ULL << 4;
    }

    return res;
}

static inline uint8_t trace_mem_build_info(
    TCGMemOp size, bool sign_extend, TCGMemOp endianness, bool store)
{
    uint8_t res = 0;
    res |= size;
    res |= (sign_extend << 2);
    if (endianness == MO_BE) {
        res |= (1ULL << 3);
    }
    res |= (store << 4);
    return res;
}

#endif /* TRACE__MEM_INTERNAL_H */
