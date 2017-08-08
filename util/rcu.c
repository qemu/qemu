/*
 * urcu-mb.c
 *
 * Userspace RCU library with explicit memory barriers
 *
 * Copyright (c) 2009 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright (c) 2009 Paul E. McKenney, IBM Corporation.
 * Copyright 2015 Red Hat, Inc.
 *
 * Ported to QEMU by Paolo Bonzini  <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/rcu.h"
#include "qemu/atomic.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"

/*
 * Global grace period counter.  Bit 0 is always one in rcu_gp_ctr.
 * Bits 1 and above are defined in synchronize_rcu.
 */
#define RCU_GP_LOCKED           (1UL << 0)
#define RCU_GP_CTR              (1UL << 1)

unsigned long rcu_gp_ctr = RCU_GP_LOCKED;

QemuEvent rcu_gp_event;
static QemuMutex rcu_registry_lock;
static QemuMutex rcu_sync_lock;

/*
 * Check whether a quiescent state was crossed between the beginning of
 * update_counter_and_wait and now.
 */
static inline int rcu_gp_ongoing(unsigned long *ctr)
{
    unsigned long v;

    v = atomic_read(ctr);
    return v && (v != rcu_gp_ctr);
}

/* Written to only by each individual reader. Read by both the reader and the
 * writers.
 */
__thread struct rcu_reader_data rcu_reader;

/* Protected by rcu_registry_lock.  */
typedef QLIST_HEAD(, rcu_reader_data) ThreadList;
static ThreadList registry = QLIST_HEAD_INITIALIZER(registry);

/* Wait for previous parity/grace period to be empty of readers.  */
static void wait_for_readers(void)
{
    ThreadList qsreaders = QLIST_HEAD_INITIALIZER(qsreaders);
    struct rcu_reader_data *index, *tmp;

    for (;;) {
        /* We want to be notified of changes made to rcu_gp_ongoing
         * while we walk the list.
         */
        qemu_event_reset(&rcu_gp_event);

        /* Instead of using atomic_mb_set for index->waiting, and
         * atomic_mb_read for index->ctr, memory barriers are placed
         * manually since writes to different threads are independent.
         * qemu_event_reset has acquire semantics, so no memory barrier
         * is needed here.
         */
        QLIST_FOREACH(index, &registry, node) {
            atomic_set(&index->waiting, true);
        }

        /* Here, order the stores to index->waiting before the
         * loads of index->ctr.
         */
        smp_mb();

        QLIST_FOREACH_SAFE(index, &registry, node, tmp) {
            if (!rcu_gp_ongoing(&index->ctr)) {
                QLIST_REMOVE(index, node);
                QLIST_INSERT_HEAD(&qsreaders, index, node);

                /* No need for mb_set here, worst of all we
                 * get some extra futex wakeups.
                 */
                atomic_set(&index->waiting, false);
            }
        }

        if (QLIST_EMPTY(&registry)) {
            break;
        }

        /* Wait for one thread to report a quiescent state and try again.
         * Release rcu_registry_lock, so rcu_(un)register_thread() doesn't
         * wait too much time.
         *
         * rcu_register_thread() may add nodes to &registry; it will not
         * wake up synchronize_rcu, but that is okay because at least another
         * thread must exit its RCU read-side critical section before
         * synchronize_rcu is done.  The next iteration of the loop will
         * move the new thread's rcu_reader from &registry to &qsreaders,
         * because rcu_gp_ongoing() will return false.
         *
         * rcu_unregister_thread() may remove nodes from &qsreaders instead
         * of &registry if it runs during qemu_event_wait.  That's okay;
         * the node then will not be added back to &registry by QLIST_SWAP
         * below.  The invariant is that the node is part of one list when
         * rcu_registry_lock is released.
         */
        qemu_mutex_unlock(&rcu_registry_lock);
        qemu_event_wait(&rcu_gp_event);
        qemu_mutex_lock(&rcu_registry_lock);
    }

    /* put back the reader list in the registry */
    QLIST_SWAP(&registry, &qsreaders, node);
}

void synchronize_rcu(void)
{
    qemu_mutex_lock(&rcu_sync_lock);
    qemu_mutex_lock(&rcu_registry_lock);

    if (!QLIST_EMPTY(&registry)) {
        /* In either case, the atomic_mb_set below blocks stores that free
         * old RCU-protected pointers.
         */
        if (sizeof(rcu_gp_ctr) < 8) {
            /* For architectures with 32-bit longs, a two-subphases algorithm
             * ensures we do not encounter overflow bugs.
             *
             * Switch parity: 0 -> 1, 1 -> 0.
             */
            atomic_mb_set(&rcu_gp_ctr, rcu_gp_ctr ^ RCU_GP_CTR);
            wait_for_readers();
            atomic_mb_set(&rcu_gp_ctr, rcu_gp_ctr ^ RCU_GP_CTR);
        } else {
            /* Increment current grace period.  */
            atomic_mb_set(&rcu_gp_ctr, rcu_gp_ctr + RCU_GP_CTR);
        }

        wait_for_readers();
    }

    qemu_mutex_unlock(&rcu_registry_lock);
    qemu_mutex_unlock(&rcu_sync_lock);
}


#define RCU_CALL_MIN_SIZE        30

/* Multi-producer, single-consumer queue based on urcu/static/wfqueue.h
 * from liburcu.  Note that head is only used by the consumer.
 */
static struct rcu_head dummy;
static struct rcu_head *head = &dummy, **tail = &dummy.next;
static int rcu_call_count;
static QemuEvent rcu_call_ready_event;

static void enqueue(struct rcu_head *node)
{
    struct rcu_head **old_tail;

    node->next = NULL;
    old_tail = atomic_xchg(&tail, &node->next);
    atomic_mb_set(old_tail, node);
}

static struct rcu_head *try_dequeue(void)
{
    struct rcu_head *node, *next;

retry:
    /* Test for an empty list, which we do not expect.  Note that for
     * the consumer head and tail are always consistent.  The head
     * is consistent because only the consumer reads/writes it.
     * The tail, because it is the first step in the enqueuing.
     * It is only the next pointers that might be inconsistent.
     */
    if (head == &dummy && atomic_mb_read(&tail) == &dummy.next) {
        abort();
    }

    /* If the head node has NULL in its next pointer, the value is
     * wrong and we need to wait until its enqueuer finishes the update.
     */
    node = head;
    next = atomic_mb_read(&head->next);
    if (!next) {
        return NULL;
    }

    /* Since we are the sole consumer, and we excluded the empty case
     * above, the queue will always have at least two nodes: the
     * dummy node, and the one being removed.  So we do not need to update
     * the tail pointer.
     */
    head = next;

    /* If we dequeued the dummy node, add it back at the end and retry.  */
    if (node == &dummy) {
        enqueue(node);
        goto retry;
    }

    return node;
}

static void *call_rcu_thread(void *opaque)
{
    struct rcu_head *node;

    rcu_register_thread();

    for (;;) {
        int tries = 0;
        int n = atomic_read(&rcu_call_count);

        /* Heuristically wait for a decent number of callbacks to pile up.
         * Fetch rcu_call_count now, we only must process elements that were
         * added before synchronize_rcu() starts.
         */
        while (n == 0 || (n < RCU_CALL_MIN_SIZE && ++tries <= 5)) {
            g_usleep(10000);
            if (n == 0) {
                qemu_event_reset(&rcu_call_ready_event);
                n = atomic_read(&rcu_call_count);
                if (n == 0) {
                    qemu_event_wait(&rcu_call_ready_event);
                }
            }
            n = atomic_read(&rcu_call_count);
        }

        atomic_sub(&rcu_call_count, n);
        synchronize_rcu();
        qemu_mutex_lock_iothread();
        while (n > 0) {
            node = try_dequeue();
            while (!node) {
                qemu_mutex_unlock_iothread();
                qemu_event_reset(&rcu_call_ready_event);
                node = try_dequeue();
                if (!node) {
                    qemu_event_wait(&rcu_call_ready_event);
                    node = try_dequeue();
                }
                qemu_mutex_lock_iothread();
            }

            n--;
            node->func(node);
        }
        qemu_mutex_unlock_iothread();
    }
    abort();
}

void call_rcu1(struct rcu_head *node, void (*func)(struct rcu_head *node))
{
    node->func = func;
    enqueue(node);
    atomic_inc(&rcu_call_count);
    qemu_event_set(&rcu_call_ready_event);
}

void rcu_register_thread(void)
{
    assert(rcu_reader.ctr == 0);
    qemu_mutex_lock(&rcu_registry_lock);
    QLIST_INSERT_HEAD(&registry, &rcu_reader, node);
    qemu_mutex_unlock(&rcu_registry_lock);
}

void rcu_unregister_thread(void)
{
    qemu_mutex_lock(&rcu_registry_lock);
    QLIST_REMOVE(&rcu_reader, node);
    qemu_mutex_unlock(&rcu_registry_lock);
}

static void rcu_init_complete(void)
{
    QemuThread thread;

    qemu_mutex_init(&rcu_registry_lock);
    qemu_mutex_init(&rcu_sync_lock);
    qemu_event_init(&rcu_gp_event, true);

    qemu_event_init(&rcu_call_ready_event, false);

    /* The caller is assumed to have iothread lock, so the call_rcu thread
     * must have been quiescent even after forking, just recreate it.
     */
    qemu_thread_create(&thread, "call_rcu", call_rcu_thread,
                       NULL, QEMU_THREAD_DETACHED);

    rcu_register_thread();
}

static int atfork_depth = 1;

void rcu_enable_atfork(void)
{
    atfork_depth++;
}

void rcu_disable_atfork(void)
{
    atfork_depth--;
}

#ifdef CONFIG_POSIX
static void rcu_init_lock(void)
{
    if (atfork_depth < 1) {
        return;
    }

    qemu_mutex_lock(&rcu_sync_lock);
    qemu_mutex_lock(&rcu_registry_lock);
}

static void rcu_init_unlock(void)
{
    if (atfork_depth < 1) {
        return;
    }

    qemu_mutex_unlock(&rcu_registry_lock);
    qemu_mutex_unlock(&rcu_sync_lock);
}

static void rcu_init_child(void)
{
    if (atfork_depth < 1) {
        return;
    }

    memset(&registry, 0, sizeof(registry));
    rcu_init_complete();
}
#endif

static void __attribute__((__constructor__)) rcu_init(void)
{
#ifdef CONFIG_POSIX
    pthread_atfork(rcu_init_lock, rcu_init_unlock, rcu_init_child);
#endif
    rcu_init_complete();
}
