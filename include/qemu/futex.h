/*
 * Wrappers around Linux futex syscall
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_FUTEX_H
#define QEMU_FUTEX_H

#include <sys/syscall.h>
#include <linux/futex.h>

#define qemu_futex(...)              syscall(__NR_futex, __VA_ARGS__)

static inline void qemu_futex_wake(void *f, int n)
{
    qemu_futex(f, FUTEX_WAKE, n, NULL, NULL, 0);
}

/*
 * Note that a wake-up can also be caused by common futex usage patterns in
 * unrelated code that happened to have previously used the futex word's
 * memory location (e.g., typical futex-based implementations of Pthreads
 * mutexes can cause this under some conditions).  Therefore, callers should
 * always conservatively assume that it is a spurious wake-up, and use the futex
 * word's value (i.e., the user-space synchronization scheme) to decide whether
 * to continue to block or not.
 */
static inline void qemu_futex_wait(void *f, unsigned val)
{
    while (qemu_futex(f, FUTEX_WAIT, (int) val, NULL, NULL, 0)) {
        switch (errno) {
        case EWOULDBLOCK:
            return;
        case EINTR:
            break; /* get out of switch and retry */
        default:
            abort();
        }
    }
}

#endif /* QEMU_FUTEX_H */
