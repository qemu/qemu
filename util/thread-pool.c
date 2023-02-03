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
#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "qemu/coroutine.h"
#include "trace.h"
#include "block/thread-pool.h"
#include "qemu/main-loop.h"

static void do_spawn_thread(ThreadPool *pool);

typedef struct ThreadPoolElement ThreadPoolElement;

enum ThreadState {
    THREAD_QUEUED,
    THREAD_ACTIVE,
    THREAD_DONE,
};

struct ThreadPoolElement {
    BlockAIOCB common;
    ThreadPool *pool;
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

    /* This list is only written by the thread pool's mother thread.  */
    QLIST_ENTRY(ThreadPoolElement) all;
};

struct ThreadPool {
    AioContext *ctx;
    QEMUBH *completion_bh;
    QemuMutex lock;
    QemuCond worker_stopped;
    QemuCond request_cond;
    QEMUBH *new_thread_bh;

    /* The following variables are only accessed from one AioContext. */
    QLIST_HEAD(, ThreadPoolElement) head;

    /* The following variables are protected by lock.  */
    QTAILQ_HEAD(, ThreadPoolElement) request_list;
    int cur_threads;
    int idle_threads;
    int new_threads;     /* backlog of threads we need to create */
    int pending_threads; /* threads created but not running yet */
    int min_threads;
    int max_threads;
};

static void *worker_thread(void *opaque)
{
    ThreadPool *pool = opaque;

    qemu_mutex_lock(&pool->lock);
    pool->pending_threads--;
    do_spawn_thread(pool);

    while (pool->cur_threads <= pool->max_threads) {
        ThreadPoolElement *req;
        int ret;

        if (QTAILQ_EMPTY(&pool->request_list)) {
            pool->idle_threads++;
            ret = qemu_cond_timedwait(&pool->request_cond, &pool->lock, 10000);
            pool->idle_threads--;
            if (ret == 0 &&
                QTAILQ_EMPTY(&pool->request_list) &&
                pool->cur_threads > pool->min_threads) {
                /* Timed out + no work to do + no need for warm threads = exit.  */
                break;
            }
            /*
             * Even if there was some work to do, check if there aren't
             * too many worker threads before picking it up.
             */
            continue;
        }

        req = QTAILQ_FIRST(&pool->request_list);
        QTAILQ_REMOVE(&pool->request_list, req, reqs);
        req->state = THREAD_ACTIVE;
        qemu_mutex_unlock(&pool->lock);

        ret = req->func(req->arg);

        req->ret = ret;
        /* Write ret before state.  */
        smp_wmb();
        req->state = THREAD_DONE;

        qemu_bh_schedule(pool->completion_bh);
        qemu_mutex_lock(&pool->lock);
    }

    pool->cur_threads--;
    qemu_cond_signal(&pool->worker_stopped);
    qemu_mutex_unlock(&pool->lock);

    /*
     * Wake up another thread, in case we got a wakeup but decided
     * to exit due to pool->cur_threads > pool->max_threads.
     */
    qemu_cond_signal(&pool->request_cond);
    return NULL;
}

static void do_spawn_thread(ThreadPool *pool)
{
    QemuThread t;

    /* Runs with lock taken.  */
    if (!pool->new_threads) {
        return;
    }

    pool->new_threads--;
    pool->pending_threads++;

    qemu_thread_create(&t, "worker", worker_thread, pool, QEMU_THREAD_DETACHED);
}

static void spawn_thread_bh_fn(void *opaque)
{
    ThreadPool *pool = opaque;

    qemu_mutex_lock(&pool->lock);
    do_spawn_thread(pool);
    qemu_mutex_unlock(&pool->lock);
}

static void spawn_thread(ThreadPool *pool)
{
    pool->cur_threads++;
    pool->new_threads++;
    /* If there are threads being created, they will spawn new workers, so
     * we don't spend time creating many threads in a loop holding a mutex or
     * starving the current vcpu.
     *
     * If there are no idle threads, ask the main thread to create one, so we
     * inherit the correct affinity instead of the vcpu affinity.
     */
    if (!pool->pending_threads) {
        qemu_bh_schedule(pool->new_thread_bh);
    }
}

static void thread_pool_completion_bh(void *opaque)
{
    ThreadPool *pool = opaque;
    ThreadPoolElement *elem, *next;

restart:
    QLIST_FOREACH_SAFE(elem, &pool->head, all, next) {
        if (elem->state != THREAD_DONE) {
            continue;
        }

        trace_thread_pool_complete(pool, elem, elem->common.opaque,
                                   elem->ret);
        QLIST_REMOVE(elem, all);

        if (elem->common.cb) {
            /* Read state before ret.  */
            smp_rmb();

            /* Schedule ourselves in case elem->common.cb() calls aio_poll() to
             * wait for another request that completed at the same time.
             */
            qemu_bh_schedule(pool->completion_bh);

            elem->common.cb(elem->common.opaque, elem->ret);

            /* We can safely cancel the completion_bh here regardless of someone
             * else having scheduled it meanwhile because we reenter the
             * completion function anyway (goto restart).
             */
            qemu_bh_cancel(pool->completion_bh);

            qemu_aio_unref(elem);
            goto restart;
        } else {
            qemu_aio_unref(elem);
        }
    }
}

static void thread_pool_cancel(BlockAIOCB *acb)
{
    ThreadPoolElement *elem = (ThreadPoolElement *)acb;
    ThreadPool *pool = elem->pool;

    trace_thread_pool_cancel(elem, elem->common.opaque);

    QEMU_LOCK_GUARD(&pool->lock);
    if (elem->state == THREAD_QUEUED) {
        QTAILQ_REMOVE(&pool->request_list, elem, reqs);
        qemu_bh_schedule(pool->completion_bh);

        elem->state = THREAD_DONE;
        elem->ret = -ECANCELED;
    }

}

static AioContext *thread_pool_get_aio_context(BlockAIOCB *acb)
{
    ThreadPoolElement *elem = (ThreadPoolElement *)acb;
    ThreadPool *pool = elem->pool;
    return pool->ctx;
}

static const AIOCBInfo thread_pool_aiocb_info = {
    .aiocb_size         = sizeof(ThreadPoolElement),
    .cancel_async       = thread_pool_cancel,
    .get_aio_context    = thread_pool_get_aio_context,
};

BlockAIOCB *thread_pool_submit_aio(ThreadPoolFunc *func, void *arg,
                                   BlockCompletionFunc *cb, void *opaque)
{
    ThreadPoolElement *req;
    AioContext *ctx = qemu_get_current_aio_context();
    ThreadPool *pool = aio_get_thread_pool(ctx);

    /* Assert that the thread submitting work is the same running the pool */
    assert(pool->ctx == qemu_get_current_aio_context());

    req = qemu_aio_get(&thread_pool_aiocb_info, NULL, cb, opaque);
    req->func = func;
    req->arg = arg;
    req->state = THREAD_QUEUED;
    req->pool = pool;

    QLIST_INSERT_HEAD(&pool->head, req, all);

    trace_thread_pool_submit(pool, req, arg);

    qemu_mutex_lock(&pool->lock);
    if (pool->idle_threads == 0 && pool->cur_threads < pool->max_threads) {
        spawn_thread(pool);
    }
    QTAILQ_INSERT_TAIL(&pool->request_list, req, reqs);
    qemu_mutex_unlock(&pool->lock);
    qemu_cond_signal(&pool->request_cond);
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
    aio_co_wake(co->co);
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

void thread_pool_update_params(ThreadPool *pool, AioContext *ctx)
{
    qemu_mutex_lock(&pool->lock);

    pool->min_threads = ctx->thread_pool_min;
    pool->max_threads = ctx->thread_pool_max;

    /*
     * We either have to:
     *  - Increase the number available of threads until over the min_threads
     *    threshold.
     *  - Bump the worker threads so that they exit, until under the max_threads
     *    threshold.
     *  - Do nothing. The current number of threads fall in between the min and
     *    max thresholds. We'll let the pool manage itself.
     */
    for (int i = pool->cur_threads; i < pool->min_threads; i++) {
        spawn_thread(pool);
    }

    for (int i = pool->cur_threads; i > pool->max_threads; i--) {
        qemu_cond_signal(&pool->request_cond);
    }

    qemu_mutex_unlock(&pool->lock);
}

static void thread_pool_init_one(ThreadPool *pool, AioContext *ctx)
{
    if (!ctx) {
        ctx = qemu_get_aio_context();
    }

    memset(pool, 0, sizeof(*pool));
    pool->ctx = ctx;
    pool->completion_bh = aio_bh_new(ctx, thread_pool_completion_bh, pool);
    qemu_mutex_init(&pool->lock);
    qemu_cond_init(&pool->worker_stopped);
    qemu_cond_init(&pool->request_cond);
    pool->new_thread_bh = aio_bh_new(ctx, spawn_thread_bh_fn, pool);

    QLIST_INIT(&pool->head);
    QTAILQ_INIT(&pool->request_list);

    thread_pool_update_params(pool, ctx);
}

ThreadPool *thread_pool_new(AioContext *ctx)
{
    ThreadPool *pool = g_new(ThreadPool, 1);
    thread_pool_init_one(pool, ctx);
    return pool;
}

void thread_pool_free(ThreadPool *pool)
{
    if (!pool) {
        return;
    }

    assert(QLIST_EMPTY(&pool->head));

    qemu_mutex_lock(&pool->lock);

    /* Stop new threads from spawning */
    qemu_bh_delete(pool->new_thread_bh);
    pool->cur_threads -= pool->new_threads;
    pool->new_threads = 0;

    /* Wait for worker threads to terminate */
    pool->max_threads = 0;
    qemu_cond_broadcast(&pool->request_cond);
    while (pool->cur_threads > 0) {
        qemu_cond_wait(&pool->worker_stopped, &pool->lock);
    }

    qemu_mutex_unlock(&pool->lock);

    qemu_bh_delete(pool->completion_bh);
    qemu_cond_destroy(&pool->request_cond);
    qemu_cond_destroy(&pool->worker_stopped);
    qemu_mutex_destroy(&pool->lock);
    g_free(pool);
}
