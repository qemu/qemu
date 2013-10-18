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
#define QEMU_SEQLOCK_H 1

#include <qemu/atomic.h>
#include <qemu/thread.h>

typedef struct QemuSeqLock QemuSeqLock;

struct QemuSeqLock {
    QemuMutex *mutex;
    unsigned sequence;
};

static inline void seqlock_init(QemuSeqLock *sl, QemuMutex *mutex)
{
    sl->mutex = mutex;
    sl->sequence = 0;
}

/* Lock out other writers and update the count.  */
static inline void seqlock_write_lock(QemuSeqLock *sl)
{
    if (sl->mutex) {
        qemu_mutex_lock(sl->mutex);
    }
    ++sl->sequence;

    /* Write sequence before updating other fields.  */
    smp_wmb();
}

static inline void seqlock_write_unlock(QemuSeqLock *sl)
{
    /* Write other fields before finalizing sequence.  */
    smp_wmb();

    ++sl->sequence;
    if (sl->mutex) {
        qemu_mutex_unlock(sl->mutex);
    }
}

static inline unsigned seqlock_read_begin(QemuSeqLock *sl)
{
    /* Always fail if a write is in progress.  */
    unsigned ret = sl->sequence & ~1;

    /* Read sequence before reading other fields.  */
    smp_rmb();
    return ret;
}

static int seqlock_read_retry(const QemuSeqLock *sl, unsigned start)
{
    /* Read other fields before reading final sequence.  */
    smp_rmb();
    return unlikely(sl->sequence != start);
}

#endif
