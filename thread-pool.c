/*
 * QEMU block layer thread pool
 *
 * Copyright IBM, Corp. 2008
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu-common.h"
#include "qemu-queue.h"
#include "qemu-thread.h"
#include "osdep.h"
#include "qemu-coroutine.h"
#include "trace.h"
#include "block_int.h"
#include "event_notifier.h"
#include "thread-pool.h"

static void do_spawn_thread(void);

typedef struct ThreadPoolElement ThreadPoolElement;

enum ThreadState {
    THREAD_QUEUED,
    THREAD_ACTIVE,
    THREAD_DONE,
    THREAD_CANCELED,
};

struct ThreadPoolElement {
    BlockDriverAIOCB common;
    ThreadPoolFunc *func;
    void *arg;

    /* Moving state out of THREAD_QUEUED is protected by lock.  After
     * that, only the worker thread can write to it.  Reads and writes
     * of state and ret are ordered with memory barriers.
     */
    enum ThreadState state;
    int ret;

    /* Access to this list is protected by lock.  */
    QTAILQ_ENTRY(ThreadPoolElement) reqs;

    /* Access to this list is protected by the global mutex.  */
    QLIST_ENTRY(ThreadPoolElement) all;
};

static EventNotifier notifier;
static QemuMutex lock;
static QemuCond check_cancel;
static QemuSemaphore sem;
static int max_threads = 64;
static QEMUBH *new_thread_bh;

/* The following variables are protected by the global mutex.  */
static QLIST_HEAD(, ThreadPoolElement) head;

/* The following variables are protected by lock.  */
static QTAILQ_HEAD(, ThreadPoolElement) request_list;
static int cur_threads;
static int idle_threads;
static int new_threads;     /* backlog of threads we need to create */
static int pending_threads; /* threads created but not running yet */
static int pending_cancellations; /* whether we need a cond_broadcast */

static void *worker_thread(void *unused)
{
    qemu_mutex_lock(&lock);
    pending_threads--;
    do_spawn_thread();

    while (1) {
        ThreadPoolElement *req;
        int ret;

        do {
            idle_threads++;
            qemu_mutex_unlock(&lock);
            ret = qemu_sem_timedwait(&sem, 10000);
            qemu_mutex_lock(&lock);
            idle_threads--;
        } while (ret == -1 && !QTAILQ_EMPTY(&request_list));
        if (ret == -1) {
            break;
        }

        req = QTAILQ_FIRST(&request_list);
        QTAILQ_REMOVE(&request_list, req, reqs);
        req->state = THREAD_ACTIVE;
        qemu_mutex_unlock(&lock);

        ret = req->func(req->arg);

        req->ret = ret;
        /* Write ret before state.  */
        smp_wmb();
        req->state = THREAD_DONE;

        qemu_mutex_lock(&lock);
        if (pending_cancellations) {
            qemu_cond_broadcast(&check_cancel);
        }

        event_notifier_set(&notifier);
    }

    cur_threads--;
    qemu_mutex_unlock(&lock);
    return NULL;
}

static void do_spawn_thread(void)
{
    QemuThread t;

    /* Runs with lock taken.  */
    if (!new_threads) {
        return;
    }

    new_threads--;
    pending_threads++;

    qemu_thread_create(&t, worker_thread, NULL, QEMU_THREAD_DETACHED);
}

static void spawn_thread_bh_fn(void *opaque)
{
    qemu_mutex_lock(&lock);
    do_spawn_thread();
    qemu_mutex_unlock(&lock);
}

static void spawn_thread(void)
{
    cur_threads++;
    new_threads++;
    /* If there are threads being created, they will spawn new workers, so
     * we don't spend time creating many threads in a loop holding a mutex or
     * starving the current vcpu.
     *
     * If there are no idle threads, ask the main thread to create one, so we
     * inherit the correct affinity instead of the vcpu affinity.
     */
    if (!pending_threads) {
        qemu_bh_schedule(new_thread_bh);
    }
}

static void event_notifier_ready(EventNotifier *notifier)
{
    ThreadPoolElement *elem, *next;

    event_notifier_test_and_clear(notifier);
restart:
    QLIST_FOREACH_SAFE(elem, &head, all, next) {
        if (elem->state != THREAD_CANCELED && elem->state != THREAD_DONE) {
            continue;
        }
        if (elem->state == THREAD_DONE) {
            trace_thread_pool_complete(elem, elem->common.opaque, elem->ret);
        }
        if (elem->state == THREAD_DONE && elem->common.cb) {
            QLIST_REMOVE(elem, all);
            /* Read state before ret.  */
            smp_rmb();
            elem->common.cb(elem->common.opaque, elem->ret);
            qemu_aio_release(elem);
            goto restart;
        } else {
            /* remove the request */
            QLIST_REMOVE(elem, all);
            qemu_aio_release(elem);
        }
    }
}

static int thread_pool_active(EventNotifier *notifier)
{
    return !QLIST_EMPTY(&head);
}

static void thread_pool_cancel(BlockDriverAIOCB *acb)
{
    ThreadPoolElement *elem = (ThreadPoolElement *)acb;

    trace_thread_pool_cancel(elem, elem->common.opaque);

    qemu_mutex_lock(&lock);
    if (elem->state == THREAD_QUEUED &&
        /* No thread has yet started working on elem. we can try to "steal"
         * the item from the worker if we can get a signal from the
         * semaphore.  Because this is non-blocking, we can do it with
         * the lock taken and ensure that elem will remain THREAD_QUEUED.
         */
        qemu_sem_timedwait(&sem, 0) == 0) {
        QTAILQ_REMOVE(&request_list, elem, reqs);
        elem->state = THREAD_CANCELED;
        event_notifier_set(&notifier);
    } else {
        pending_cancellations++;
        while (elem->state != THREAD_CANCELED && elem->state != THREAD_DONE) {
            qemu_cond_wait(&check_cancel, &lock);
        }
        pending_cancellations--;
    }
    qemu_mutex_unlock(&lock);
}

static const AIOCBInfo thread_pool_aiocb_info = {
    .aiocb_size         = sizeof(ThreadPoolElement),
    .cancel             = thread_pool_cancel,
};

BlockDriverAIOCB *thread_pool_submit_aio(ThreadPoolFunc *func, void *arg,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    ThreadPoolElement *req;

    req = qemu_aio_get(&thread_pool_aiocb_info, NULL, cb, opaque);
    req->func = func;
    req->arg = arg;
    req->state = THREAD_QUEUED;

    QLIST_INSERT_HEAD(&head, req, all);

    trace_thread_pool_submit(req, arg);

    qemu_mutex_lock(&lock);
    if (idle_threads == 0 && cur_threads < max_threads) {
        spawn_thread();
    }
    QTAILQ_INSERT_TAIL(&request_list, req, reqs);
    qemu_mutex_unlock(&lock);
    qemu_sem_post(&sem);
    return &req->common;
}

typedef struct ThreadPoolCo {
    Coroutine *co;
    int ret;
} ThreadPoolCo;

static void thread_pool_co_cb(void *opaque, int ret)
{
    ThreadPoolCo *co = opaque;

    co->ret = ret;
    qemu_coroutine_enter(co->co, NULL);
}

int coroutine_fn thread_pool_submit_co(ThreadPoolFunc *func, void *arg)
{
    ThreadPoolCo tpc = { .co = qemu_coroutine_self(), .ret = -EINPROGRESS };
    assert(qemu_in_coroutine());
    thread_pool_submit_aio(func, arg, thread_pool_co_cb, &tpc);
    qemu_coroutine_yield();
    return tpc.ret;
}

void thread_pool_submit(ThreadPoolFunc *func, void *arg)
{
    thread_pool_submit_aio(func, arg, NULL, NULL);
}

static void thread_pool_init(void)
{
    QLIST_INIT(&head);
    event_notifier_init(&notifier, false);
    qemu_mutex_init(&lock);
    qemu_cond_init(&check_cancel);
    qemu_sem_init(&sem, 0);
    qemu_aio_set_event_notifier(&notifier, event_notifier_ready,
                                thread_pool_active);

    QTAILQ_INIT(&request_list);
    new_thread_bh = qemu_bh_new(spawn_thread_bh_fn, NULL);
}

block_init(thread_pool_init)
