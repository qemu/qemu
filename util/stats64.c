/*
 * Atomic operations on 64-bit quantities.
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/stats64.h"
#include "qemu/processor.h"

#ifndef CONFIG_ATOMIC64
static inline void stat64_rdlock(Stat64 *s)
{
    /* Keep out incoming writers to avoid them starving us. */
    qatomic_add(&s->lock, 2);

    /* If there is a concurrent writer, wait for it.  */
    while (qatomic_read(&s->lock) & 1) {
        cpu_relax();
    }
}

static inline void stat64_rdunlock(Stat64 *s)
{
    qatomic_sub(&s->lock, 2);
}

static inline bool stat64_wrtrylock(Stat64 *s)
{
    return qatomic_cmpxchg(&s->lock, 0, 1) == 0;
}

static inline void stat64_wrunlock(Stat64 *s)
{
    qatomic_dec(&s->lock);
}

uint64_t stat64_get(const Stat64 *s)
{
    uint32_t high, low;

    stat64_rdlock((Stat64 *)s);

    /* 64-bit writes always take the lock, so we can read in
     * any order.
     */
    high = qatomic_read(&s->high);
    low = qatomic_read(&s->low);
    stat64_rdunlock((Stat64 *)s);

    return ((uint64_t)high << 32) | low;
}

bool stat64_add32_carry(Stat64 *s, uint32_t low, uint32_t high)
{
    uint32_t old;

    if (!stat64_wrtrylock(s)) {
        cpu_relax();
        return false;
    }

    /* 64-bit reads always take the lock, so they don't care about the
     * order of our update.  By updating s->low first, we can check
     * whether we have to carry into s->high.
     */
    old = qatomic_fetch_add(&s->low, low);
    high += (old + low) < old;
    qatomic_add(&s->high, high);
    stat64_wrunlock(s);
    return true;
}

bool stat64_min_slow(Stat64 *s, uint64_t value)
{
    uint32_t high, low;
    uint64_t orig;

    if (!stat64_wrtrylock(s)) {
        cpu_relax();
        return false;
    }

    high = qatomic_read(&s->high);
    low = qatomic_read(&s->low);

    orig = ((uint64_t)high << 32) | low;
    if (value < orig) {
        /* We have to set low before high, just like stat64_min reads
         * high before low.  The value may become higher temporarily, but
         * stat64_get does not notice (it takes the lock) and the only ill
         * effect on stat64_min is that the slow path may be triggered
         * unnecessarily.
         */
        qatomic_set(&s->low, (uint32_t)value);
        smp_wmb();
        qatomic_set(&s->high, value >> 32);
    }
    stat64_wrunlock(s);
    return true;
}

bool stat64_max_slow(Stat64 *s, uint64_t value)
{
    uint32_t high, low;
    uint64_t orig;

    if (!stat64_wrtrylock(s)) {
        cpu_relax();
        return false;
    }

    high = qatomic_read(&s->high);
    low = qatomic_read(&s->low);

    orig = ((uint64_t)high << 32) | low;
    if (value > orig) {
        /* We have to set low before high, just like stat64_max reads
         * high before low.  The value may become lower temporarily, but
         * stat64_get does not notice (it takes the lock) and the only ill
         * effect on stat64_max is that the slow path may be triggered
         * unnecessarily.
         */
        qatomic_set(&s->low, (uint32_t)value);
        smp_wmb();
        qatomic_set(&s->high, value >> 32);
    }
    stat64_wrunlock(s);
    return true;
}
#endif
