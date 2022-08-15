/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/*
 * Accessed in parallel; all accesses to 'tb' must be atomic.
 */
struct CPUJumpCache {
    struct {
        TranslationBlock *tb;
    } array[TB_JMP_CACHE_SIZE];
};

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
