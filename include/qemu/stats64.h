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

#ifndef QEMU_STATS64_H
#define QEMU_STATS64_H

#include "qemu/atomic.h"

/* This provides atomic operations on 64-bit type, using a reader-writer
 * spinlock on architectures that do not have 64-bit accesses.  Even on
 * those architectures, it tries hard not to take the lock.
 */

typedef struct Stat64 {
#ifdef CONFIG_ATOMIC64
    uint64_t value;
#else
    uint32_t low, high;
    uint32_t lock;
#endif
} Stat64;

#ifdef CONFIG_ATOMIC64
static inline void stat64_init(Stat64 *s, uint64_t value)
{
    /* This is not guaranteed to be atomic! */
    *s = (Stat64) { value };
}

static inline uint64_t stat64_get(const Stat64 *s)
{
    return qatomic_read__nocheck(&s->value);
}

static inline void stat64_add(Stat64 *s, uint64_t value)
{
    qatomic_add(&s->value, value);
}

static inline void stat64_min(Stat64 *s, uint64_t value)
{
    uint64_t orig = qatomic_read__nocheck(&s->value);
    while (orig > value) {
        orig = qatomic_cmpxchg__nocheck(&s->value, orig, value);
    }
}

static inline void stat64_max(Stat64 *s, uint64_t value)
{
    uint64_t orig = qatomic_read__nocheck(&s->value);
    while (orig < value) {
        orig = qatomic_cmpxchg__nocheck(&s->value, orig, value);
    }
}
#else
uint64_t stat64_get(const Stat64 *s);
bool stat64_min_slow(Stat64 *s, uint64_t value);
bool stat64_max_slow(Stat64 *s, uint64_t value);
bool stat64_add32_carry(Stat64 *s, uint32_t low, uint32_t high);

static inline void stat64_init(Stat64 *s, uint64_t value)
{
    /* This is not guaranteed to be atomic! */
    *s = (Stat64) { .low = value, .high = value >> 32, .lock = 0 };
}

static inline void stat64_add(Stat64 *s, uint64_t value)
{
    uint32_t low, high;
    high = value >> 32;
    low = (uint32_t) value;
    if (!low) {
        if (high) {
            qatomic_add(&s->high, high);
        }
        return;
    }

    for (;;) {
        uint32_t orig = s->low;
        uint32_t result = orig + low;
        uint32_t old;

        if (result < low || high) {
            /* If the high part is affected, take the lock.  */
            if (stat64_add32_carry(s, low, high)) {
                return;
            }
            continue;
        }

        /* No carry, try with a 32-bit cmpxchg.  The result is independent of
         * the high 32 bits, so it can race just fine with stat64_add32_carry
         * and even stat64_get!
         */
        old = qatomic_cmpxchg(&s->low, orig, result);
        if (orig == old) {
            return;
        }
    }
}

static inline void stat64_min(Stat64 *s, uint64_t value)
{
    uint32_t low, high;
    uint32_t orig_low, orig_high;

    high = value >> 32;
    low = (uint32_t) value;
    do {
        orig_high = qatomic_read(&s->high);
        if (orig_high < high) {
            return;
        }

        if (orig_high == high) {
            /* High 32 bits are equal.  Read low after high, otherwise we
             * can get a false positive (e.g. 0x1235,0x0000 changes to
             * 0x1234,0x8000 and we read it as 0x1234,0x0000). Pairs with
             * the write barrier in stat64_min_slow.
             */
            smp_rmb();
            orig_low = qatomic_read(&s->low);
            if (orig_low <= low) {
                return;
            }

            /* See if we were lucky and a writer raced against us.  The
             * barrier is theoretically unnecessary, but if we remove it
             * we may miss being lucky.
             */
            smp_rmb();
            orig_high = qatomic_read(&s->high);
            if (orig_high < high) {
                return;
            }
        }

        /* If the value changes in any way, we have to take the lock.  */
    } while (!stat64_min_slow(s, value));
}

static inline void stat64_max(Stat64 *s, uint64_t value)
{
    uint32_t low, high;
    uint32_t orig_low, orig_high;

    high = value >> 32;
    low = (uint32_t) value;
    do {
        orig_high = qatomic_read(&s->high);
        if (orig_high > high) {
            return;
        }

        if (orig_high == high) {
            /* High 32 bits are equal.  Read low after high, otherwise we
             * can get a false positive (e.g. 0x1234,0x8000 changes to
             * 0x1235,0x0000 and we read it as 0x1235,0x8000). Pairs with
             * the write barrier in stat64_max_slow.
             */
            smp_rmb();
            orig_low = qatomic_read(&s->low);
            if (orig_low >= low) {
                return;
            }

            /* See if we were lucky and a writer raced against us.  The
             * barrier is theoretically unnecessary, but if we remove it
             * we may miss being lucky.
             */
            smp_rmb();
            orig_high = qatomic_read(&s->high);
            if (orig_high > high) {
                return;
            }
        }

        /* If the value changes in any way, we have to take the lock.  */
    } while (!stat64_max_slow(s, value));
}

#endif

#endif
