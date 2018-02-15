/*
 * Polymorphic locking functions (aka poor man templates)
 *
 * Copyright Red Hat, Inc. 2017, 2018
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_LOCKABLE_H
#define QEMU_LOCKABLE_H

#include "qemu/coroutine.h"
#include "qemu/thread.h"

typedef void QemuLockUnlockFunc(void *);

struct QemuLockable {
    void *object;
    QemuLockUnlockFunc *lock;
    QemuLockUnlockFunc *unlock;
};

/* This function gives an error if an invalid, non-NULL pointer type is passed
 * to QEMU_MAKE_LOCKABLE.  For optimized builds, we can rely on dead-code elimination
 * from the compiler, and give the errors already at link time.
 */
#if defined(__OPTIMIZE__) && !defined(__SANITIZE_ADDRESS__)
void unknown_lock_type(void *);
#else
static inline void unknown_lock_type(void *unused)
{
    abort();
}
#endif

static inline __attribute__((__always_inline__)) QemuLockable *
qemu_make_lockable(void *x, QemuLockable *lockable)
{
    /* We cannot test this in a macro, otherwise we get compiler
     * warnings like "the address of 'm' will always evaluate as 'true'".
     */
    return x ? lockable : NULL;
}

/* Auxiliary macros to simplify QEMU_MAKE_LOCABLE.  */
#define QEMU_LOCK_FUNC(x) ((QemuLockUnlockFunc *)    \
    QEMU_GENERIC(x,                                  \
                 (QemuMutex *, qemu_mutex_lock),     \
                 (CoMutex *, qemu_co_mutex_lock),    \
                 (QemuSpin *, qemu_spin_lock),       \
                 unknown_lock_type))

#define QEMU_UNLOCK_FUNC(x) ((QemuLockUnlockFunc *)  \
    QEMU_GENERIC(x,                                  \
                 (QemuMutex *, qemu_mutex_unlock),   \
                 (CoMutex *, qemu_co_mutex_unlock),  \
                 (QemuSpin *, qemu_spin_unlock),     \
                 unknown_lock_type))

/* In C, compound literals have the lifetime of an automatic variable.
 * In C++ it would be different, but then C++ wouldn't need QemuLockable
 * either...
 */
#define QEMU_MAKE_LOCKABLE_(x) qemu_make_lockable((x), &(QemuLockable) {    \
        .object = (x),                               \
        .lock = QEMU_LOCK_FUNC(x),                   \
        .unlock = QEMU_UNLOCK_FUNC(x),               \
    })

/* QEMU_MAKE_LOCKABLE - Make a polymorphic QemuLockable
 *
 * @x: a lock object (currently one of QemuMutex, CoMutex, QemuSpin).
 *
 * Returns a QemuLockable object that can be passed around
 * to a function that can operate with locks of any kind.
 */
#define QEMU_MAKE_LOCKABLE(x)                        \
    QEMU_GENERIC(x,                                  \
                 (QemuLockable *, (x)),              \
                 QEMU_MAKE_LOCKABLE_(x))

static inline void qemu_lockable_lock(QemuLockable *x)
{
    x->lock(x->object);
}

static inline void qemu_lockable_unlock(QemuLockable *x)
{
    x->unlock(x->object);
}

#endif
