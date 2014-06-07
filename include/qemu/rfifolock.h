/*
 * Recursive FIFO lock
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_RFIFOLOCK_H
#define QEMU_RFIFOLOCK_H

#include "qemu/thread.h"

/* Recursive FIFO lock
 *
 * This lock provides more features than a plain mutex:
 *
 * 1. Fairness - enforces FIFO order.
 * 2. Nesting - can be taken recursively.
 * 3. Contention callback - optional, called when thread must wait.
 *
 * The recursive FIFO lock is heavyweight so prefer other synchronization
 * primitives if you do not need its features.
 */
typedef struct {
    QemuMutex lock;             /* protects all fields */

    /* FIFO order */
    unsigned int head;          /* active ticket number */
    unsigned int tail;          /* waiting ticket number */
    QemuCond cond;              /* used to wait for our ticket number */

    /* Nesting */
    QemuThread owner_thread;    /* thread that currently has ownership */
    unsigned int nesting;       /* amount of nesting levels */

    /* Contention callback */
    void (*cb)(void *);         /* called when thread must wait, with ->lock
                                 * held so it may not recursively lock/unlock
                                 */
    void *cb_opaque;
} RFifoLock;

void rfifolock_init(RFifoLock *r, void (*cb)(void *), void *opaque);
void rfifolock_destroy(RFifoLock *r);
void rfifolock_lock(RFifoLock *r);
void rfifolock_unlock(RFifoLock *r);

#endif /* QEMU_RFIFOLOCK_H */
