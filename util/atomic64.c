/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"

#ifdef CONFIG_ATOMIC64
#error This file must only be compiled if !CONFIG_ATOMIC64
#endif

/*
 * When !CONFIG_ATOMIC64, we serialize both reads and writes with spinlocks.
 * We use an array of spinlocks, with padding computed at run-time based on
 * the host's dcache line size.
 * We point to the array with a void * to simplify the padding's computation.
 * Each spinlock is located every lock_size bytes.
 */
static void *lock_array;
static size_t lock_size;

/*
 * Systems without CONFIG_ATOMIC64 are unlikely to have many cores, so we use a
 * small array of locks.
 */
#define NR_LOCKS 16

static QemuSpin *addr_to_lock(const void *addr)
{
    uintptr_t a = (uintptr_t)addr;
    uintptr_t idx;

    idx = a >> qemu_dcache_linesize_log;
    idx ^= (idx >> 8) ^ (idx >> 16);
    idx &= NR_LOCKS - 1;
    return lock_array + idx * lock_size;
}

#define GEN_READ(name, type)                    \
    type name(const type *ptr)                  \
    {                                           \
        QemuSpin *lock = addr_to_lock(ptr);     \
        type ret;                               \
                                                \
        qemu_spin_lock(lock);                   \
        ret = *ptr;                             \
        qemu_spin_unlock(lock);                 \
        return ret;                             \
    }

GEN_READ(qatomic_read_i64, int64_t)
GEN_READ(qatomic_read_u64, uint64_t)
#undef GEN_READ

#define GEN_SET(name, type)                     \
    void name(type *ptr, type val)              \
    {                                           \
        QemuSpin *lock = addr_to_lock(ptr);     \
                                                \
        qemu_spin_lock(lock);                   \
        *ptr = val;                             \
        qemu_spin_unlock(lock);                 \
    }

GEN_SET(qatomic_set_i64, int64_t)
GEN_SET(qatomic_set_u64, uint64_t)
#undef GEN_SET

void qatomic64_init(void)
{
    int i;

    lock_size = ROUND_UP(sizeof(QemuSpin), qemu_dcache_linesize);
    lock_array = qemu_memalign(qemu_dcache_linesize, lock_size * NR_LOCKS);
    for (i = 0; i < NR_LOCKS; i++) {
        QemuSpin *lock = lock_array + i * lock_size;

        qemu_spin_init(lock);
    }
}
