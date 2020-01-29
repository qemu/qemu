/*
 * Seqlock implementation for QEMU
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEQLOCK_H
#define QEMU_SEQLOCK_H

#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "qemu/lockable.h"

typedef struct QemuSeqLock QemuSeqLock;

struct QemuSeqLock {
    unsigned sequence;
};

static inline void seqlock_init(QemuSeqLock *sl)
{
    sl->sequence = 0;
}

/* Lock out other writers and update the count.  */
static inline void seqlock_write_begin(QemuSeqLock *sl)
{
    atomic_set(&sl->sequence, sl->sequence + 1);

    /* Write sequence before updating other fields.  */
    smp_wmb();
}

static inline void seqlock_write_end(QemuSeqLock *sl)
{
    /* Write other fields before finalizing sequence.  */
    smp_wmb();

    atomic_set(&sl->sequence, sl->sequence + 1);
}

/* Lock out other writers and update the count.  */
static inline void seqlock_write_lock_impl(QemuSeqLock *sl, QemuLockable *lock)
{
    qemu_lockable_lock(lock);
    seqlock_write_begin(sl);
}
#define seqlock_write_lock(sl, lock) \
    seqlock_write_lock_impl(sl, QEMU_MAKE_LOCKABLE(lock))

/* Update the count and release the lock.  */
static inline void seqlock_write_unlock_impl(QemuSeqLock *sl, QemuLockable *lock)
{
    seqlock_write_end(sl);
    qemu_lockable_unlock(lock);
}
#define seqlock_write_unlock(sl, lock) \
    seqlock_write_unlock_impl(sl, QEMU_MAKE_LOCKABLE(lock))


static inline unsigned seqlock_read_begin(const QemuSeqLock *sl)
{
    /* Always fail if a write is in progress.  */
    unsigned ret = atomic_read(&sl->sequence);

    /* Read sequence before reading other fields.  */
    smp_rmb();
    return ret & ~1;
}

static inline int seqlock_read_retry(const QemuSeqLock *sl, unsigned start)
{
    /* Read other fields before reading final sequence.  */
    smp_rmb();
    return unlikely(atomic_read(&sl->sequence) != start);
}

#endif
