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
 * For CF_PCREL, accesses to 'pc' must be protected by a
 * load_acquire/store_release to 'tb'.
 */
struct CPUJumpCache {
    struct rcu_head rcu;
    struct {
        TranslationBlock *tb;
        target_ulong pc;
    } array[TB_JMP_CACHE_SIZE];
};

static inline TranslationBlock *
tb_jmp_cache_get_tb(CPUJumpCache *jc, uint32_t cflags, uint32_t hash)
{
    if (cflags & CF_PCREL) {
        /* Use acquire to ensure current load of pc from jc. */
        return qatomic_load_acquire(&jc->array[hash].tb);
    } else {
        /* Use rcu_read to ensure current load of pc from *tb. */
        return qatomic_rcu_read(&jc->array[hash].tb);
    }
}

static inline target_ulong
tb_jmp_cache_get_pc(CPUJumpCache *jc, uint32_t hash, TranslationBlock *tb)
{
    if (tb_cflags(tb) & CF_PCREL) {
        return jc->array[hash].pc;
    } else {
        return tb_pc(tb);
    }
}

static inline void
tb_jmp_cache_set(CPUJumpCache *jc, uint32_t hash,
                 TranslationBlock *tb, target_ulong pc)
{
    if (tb_cflags(tb) & CF_PCREL) {
        jc->array[hash].pc = pc;
        /* Use store_release on tb to ensure pc is written first. */
        qatomic_store_release(&jc->array[hash].tb, tb);
    } else{
        /* Use the pc value already stored in tb->pc. */
        qatomic_set(&jc->array[hash].tb, tb);
    }
}

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
