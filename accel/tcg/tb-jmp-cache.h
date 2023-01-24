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
 * For TARGET_TB_PCREL, accesses to 'pc' must be protected by
 * a load_acquire/store_release to 'tb'.
 */
struct CPUJumpCache {
    struct rcu_head rcu;
    struct {
        TranslationBlock *tb;
#if TARGET_TB_PCREL
        target_ulong pc;
#endif
    } array[TB_JMP_CACHE_SIZE];
};

static inline TranslationBlock *
tb_jmp_cache_get_tb(CPUJumpCache *jc, uint32_t hash)
{
#if TARGET_TB_PCREL
    /* Use acquire to ensure current load of pc from jc. */
    return qatomic_load_acquire(&jc->array[hash].tb);
#else
    /* Use rcu_read to ensure current load of pc from *tb. */
    return qatomic_rcu_read(&jc->array[hash].tb);
#endif
}

static inline target_ulong
tb_jmp_cache_get_pc(CPUJumpCache *jc, uint32_t hash, TranslationBlock *tb)
{
#if TARGET_TB_PCREL
    return jc->array[hash].pc;
#else
    return tb_pc(tb);
#endif
}

static inline void
tb_jmp_cache_set(CPUJumpCache *jc, uint32_t hash,
                 TranslationBlock *tb, target_ulong pc)
{
#if TARGET_TB_PCREL
    jc->array[hash].pc = pc;
    /* Use store_release on tb to ensure pc is written first. */
    qatomic_store_release(&jc->array[hash].tb, tb);
#else
    /* Use the pc value already stored in tb->pc. */
    qatomic_set(&jc->array[hash].tb, tb);
#endif
}

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
