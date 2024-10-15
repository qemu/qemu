/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QemuLockCnt implementation
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *   Paolo Bonzini <pbonzini@redhat.com>
 *
 */

#ifndef QEMU_LOCKCNT_H
#define QEMU_LOCKCNT_H

#include "qemu/thread.h"

typedef struct QemuLockCnt QemuLockCnt;

struct QemuLockCnt {
#ifndef CONFIG_LINUX
    QemuMutex mutex;
#endif
    unsigned count;
};

/**
 * qemu_lockcnt_init: initialize a QemuLockcnt
 * @lockcnt: the lockcnt to initialize
 *
 * Initialize lockcnt's counter to zero and prepare its mutex
 * for usage.
 */
void qemu_lockcnt_init(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_destroy: destroy a QemuLockcnt
 * @lockcnt: the lockcnt to destruct
 *
 * Destroy lockcnt's mutex.
 */
void qemu_lockcnt_destroy(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_inc: increment a QemuLockCnt's counter
 * @lockcnt: the lockcnt to operate on
 *
 * If the lockcnt's count is zero, wait for critical sections
 * to finish and increment lockcnt's count to 1.  If the count
 * is not zero, just increment it.
 *
 * Because this function can wait on the mutex, it must not be
 * called while the lockcnt's mutex is held by the current thread.
 * For the same reason, qemu_lockcnt_inc can also contribute to
 * AB-BA deadlocks.  This is a sample deadlock scenario::
 *
 *            thread 1                      thread 2
 *            -------------------------------------------------------
 *            qemu_lockcnt_lock(&lc1);
 *                                          qemu_lockcnt_lock(&lc2);
 *            qemu_lockcnt_inc(&lc2);
 *                                          qemu_lockcnt_inc(&lc1);
 */
void qemu_lockcnt_inc(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec: decrement a QemuLockCnt's counter
 * @lockcnt: the lockcnt to operate on
 */
void qemu_lockcnt_dec(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec_and_lock: decrement a QemuLockCnt's counter and
 * possibly lock it.
 * @lockcnt: the lockcnt to operate on
 *
 * Decrement lockcnt's count.  If the new count is zero, lock
 * the mutex and return true.  Otherwise, return false.
 */
bool qemu_lockcnt_dec_and_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec_if_lock: possibly decrement a QemuLockCnt's counter and
 * lock it.
 * @lockcnt: the lockcnt to operate on
 *
 * If the count is 1, decrement the count to zero, lock
 * the mutex and return true.  Otherwise, return false.
 */
bool qemu_lockcnt_dec_if_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_lock: lock a QemuLockCnt's mutex.
 * @lockcnt: the lockcnt to operate on
 *
 * Remember that concurrent visits are not blocked unless the count is
 * also zero.  You can use qemu_lockcnt_count to check for this inside a
 * critical section.
 */
void qemu_lockcnt_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_unlock: release a QemuLockCnt's mutex.
 * @lockcnt: the lockcnt to operate on.
 */
void qemu_lockcnt_unlock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_inc_and_unlock: combined unlock/increment on a QemuLockCnt.
 * @lockcnt: the lockcnt to operate on.
 *
 * This is the same as
 *
 *     qemu_lockcnt_unlock(lockcnt);
 *     qemu_lockcnt_inc(lockcnt);
 *
 * but more efficient.
 */
void qemu_lockcnt_inc_and_unlock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_count: query a LockCnt's count.
 * @lockcnt: the lockcnt to query.
 *
 * Note that the count can change at any time.  Still, while the
 * lockcnt is locked, one can usefully check whether the count
 * is non-zero.
 */
unsigned qemu_lockcnt_count(QemuLockCnt *lockcnt);

#endif
