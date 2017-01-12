/*
 * QemuLockCnt implementation
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *   Paolo Bonzini <pbonzini@redhat.com>
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/atomic.h"

void qemu_lockcnt_init(QemuLockCnt *lockcnt)
{
    qemu_mutex_init(&lockcnt->mutex);
    lockcnt->count = 0;
}

void qemu_lockcnt_destroy(QemuLockCnt *lockcnt)
{
    qemu_mutex_destroy(&lockcnt->mutex);
}

void qemu_lockcnt_inc(QemuLockCnt *lockcnt)
{
    int old;
    for (;;) {
        old = atomic_read(&lockcnt->count);
        if (old == 0) {
            qemu_lockcnt_lock(lockcnt);
            qemu_lockcnt_inc_and_unlock(lockcnt);
            return;
        } else {
            if (atomic_cmpxchg(&lockcnt->count, old, old + 1) == old) {
                return;
            }
        }
    }
}

void qemu_lockcnt_dec(QemuLockCnt *lockcnt)
{
    atomic_dec(&lockcnt->count);
}

/* Decrement a counter, and return locked if it is decremented to zero.
 * It is impossible for the counter to become nonzero while the mutex
 * is taken.
 */
bool qemu_lockcnt_dec_and_lock(QemuLockCnt *lockcnt)
{
    int val = atomic_read(&lockcnt->count);
    while (val > 1) {
        int old = atomic_cmpxchg(&lockcnt->count, val, val - 1);
        if (old != val) {
            val = old;
            continue;
        }

        return false;
    }

    qemu_lockcnt_lock(lockcnt);
    if (atomic_fetch_dec(&lockcnt->count) == 1) {
        return true;
    }

    qemu_lockcnt_unlock(lockcnt);
    return false;
}

/* Decrement a counter and return locked if it is decremented to zero.
 * Otherwise do nothing.
 *
 * It is impossible for the counter to become nonzero while the mutex
 * is taken.
 */
bool qemu_lockcnt_dec_if_lock(QemuLockCnt *lockcnt)
{
    /* No need for acquire semantics if we return false.  */
    int val = atomic_read(&lockcnt->count);
    if (val > 1) {
        return false;
    }

    qemu_lockcnt_lock(lockcnt);
    if (atomic_fetch_dec(&lockcnt->count) == 1) {
        return true;
    }

    qemu_lockcnt_inc_and_unlock(lockcnt);
    return false;
}

void qemu_lockcnt_lock(QemuLockCnt *lockcnt)
{
    qemu_mutex_lock(&lockcnt->mutex);
}

void qemu_lockcnt_inc_and_unlock(QemuLockCnt *lockcnt)
{
    atomic_inc(&lockcnt->count);
    qemu_mutex_unlock(&lockcnt->mutex);
}

void qemu_lockcnt_unlock(QemuLockCnt *lockcnt)
{
    qemu_mutex_unlock(&lockcnt->mutex);
}

unsigned qemu_lockcnt_count(QemuLockCnt *lockcnt)
{
    return atomic_read(&lockcnt->count);
}
