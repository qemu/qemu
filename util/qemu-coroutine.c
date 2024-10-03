/*
 * QEMU coroutines
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Kevin Wolf         <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "trace.h"
#include "qemu/thread.h"
#include "qemu/atomic.h"
#include "qemu/coroutine_int.h"
#include "qemu/coroutine-tls.h"
#include "qemu/cutils.h"
#include "block/aio.h"

enum {
    COROUTINE_POOL_BATCH_MAX_SIZE = 128,
};

/*
 * Coroutine creation and deletion is expensive so a pool of unused coroutines
 * is kept as a cache. When the pool has coroutines available, they are
 * recycled instead of creating new ones from scratch. Coroutines are added to
 * the pool upon termination.
 *
 * The pool is global but each thread maintains a small local pool to avoid
 * global pool contention. Threads fetch and return batches of coroutines from
 * the global pool to maintain their local pool. The local pool holds up to two
 * batches whereas the maximum size of the global pool is controlled by the
 * qemu_coroutine_inc_pool_size() API.
 *
 * .-----------------------------------.
 * | Batch 1 | Batch 2 | Batch 3 | ... | global_pool
 * `-----------------------------------'
 *
 * .-------------------.
 * | Batch 1 | Batch 2 | per-thread local_pool (maximum 2 batches)
 * `-------------------'
 */
typedef struct CoroutinePoolBatch {
    /* Batches are kept in a list */
    QSLIST_ENTRY(CoroutinePoolBatch) next;

    /* This batch holds up to @COROUTINE_POOL_BATCH_MAX_SIZE coroutines */
    QSLIST_HEAD(, Coroutine) list;
    unsigned int size;
} CoroutinePoolBatch;

typedef QSLIST_HEAD(, CoroutinePoolBatch) CoroutinePool;

/* Host operating system limit on number of pooled coroutines */
static unsigned int global_pool_hard_max_size;

static QemuMutex global_pool_lock; /* protects the following variables */
static CoroutinePool global_pool = QSLIST_HEAD_INITIALIZER(global_pool);
static unsigned int global_pool_size;
static unsigned int global_pool_max_size = COROUTINE_POOL_BATCH_MAX_SIZE;

QEMU_DEFINE_STATIC_CO_TLS(CoroutinePool, local_pool);
QEMU_DEFINE_STATIC_CO_TLS(Notifier, local_pool_cleanup_notifier);

static CoroutinePoolBatch *coroutine_pool_batch_new(void)
{
    CoroutinePoolBatch *batch = g_new(CoroutinePoolBatch, 1);

    QSLIST_INIT(&batch->list);
    batch->size = 0;
    return batch;
}

static void coroutine_pool_batch_delete(CoroutinePoolBatch *batch)
{
    Coroutine *co;
    Coroutine *tmp;

    QSLIST_FOREACH_SAFE(co, &batch->list, pool_next, tmp) {
        QSLIST_REMOVE_HEAD(&batch->list, pool_next);
        qemu_coroutine_delete(co);
    }
    g_free(batch);
}

static void local_pool_cleanup(Notifier *n, void *value)
{
    CoroutinePool *local_pool = get_ptr_local_pool();
    CoroutinePoolBatch *batch;
    CoroutinePoolBatch *tmp;

    QSLIST_FOREACH_SAFE(batch, local_pool, next, tmp) {
        QSLIST_REMOVE_HEAD(local_pool, next);
        coroutine_pool_batch_delete(batch);
    }
}

/* Ensure the atexit notifier is registered */
static void local_pool_cleanup_init_once(void)
{
    Notifier *notifier = get_ptr_local_pool_cleanup_notifier();
    if (!notifier->notify) {
        notifier->notify = local_pool_cleanup;
        qemu_thread_atexit_add(notifier);
    }
}

/* Helper to get the next unused coroutine from the local pool */
static Coroutine *coroutine_pool_get_local(void)
{
    CoroutinePool *local_pool = get_ptr_local_pool();
    CoroutinePoolBatch *batch = QSLIST_FIRST(local_pool);
    Coroutine *co;

    if (unlikely(!batch)) {
        return NULL;
    }

    co = QSLIST_FIRST(&batch->list);
    QSLIST_REMOVE_HEAD(&batch->list, pool_next);
    batch->size--;

    if (batch->size == 0) {
        QSLIST_REMOVE_HEAD(local_pool, next);
        coroutine_pool_batch_delete(batch);
    }
    return co;
}

/* Get the next batch from the global pool */
static void coroutine_pool_refill_local(void)
{
    CoroutinePool *local_pool = get_ptr_local_pool();
    CoroutinePoolBatch *batch = NULL;

    WITH_QEMU_LOCK_GUARD(&global_pool_lock) {
        batch = QSLIST_FIRST(&global_pool);

        if (batch) {
            QSLIST_REMOVE_HEAD(&global_pool, next);
            global_pool_size -= batch->size;
        }
    }

    if (batch) {
        QSLIST_INSERT_HEAD(local_pool, batch, next);
        local_pool_cleanup_init_once();
    }
}

/* Add a batch of coroutines to the global pool */
static void coroutine_pool_put_global(CoroutinePoolBatch *batch)
{
    WITH_QEMU_LOCK_GUARD(&global_pool_lock) {
        unsigned int max = MIN(global_pool_max_size,
                               global_pool_hard_max_size);

        if (global_pool_size < max) {
            QSLIST_INSERT_HEAD(&global_pool, batch, next);

            /* Overshooting the max pool size is allowed */
            global_pool_size += batch->size;
            return;
        }
    }

    /* The global pool was full, so throw away this batch */
    coroutine_pool_batch_delete(batch);
}

/* Get the next unused coroutine from the pool or return NULL */
static Coroutine *coroutine_pool_get(void)
{
    Coroutine *co;

    co = coroutine_pool_get_local();
    if (!co) {
        coroutine_pool_refill_local();
        co = coroutine_pool_get_local();
    }
    return co;
}

static void coroutine_pool_put(Coroutine *co)
{
    CoroutinePool *local_pool = get_ptr_local_pool();
    CoroutinePoolBatch *batch = QSLIST_FIRST(local_pool);

    if (unlikely(!batch)) {
        batch = coroutine_pool_batch_new();
        QSLIST_INSERT_HEAD(local_pool, batch, next);
        local_pool_cleanup_init_once();
    }

    if (unlikely(batch->size >= COROUTINE_POOL_BATCH_MAX_SIZE)) {
        CoroutinePoolBatch *next = QSLIST_NEXT(batch, next);

        /* Is the local pool full? */
        if (next) {
            QSLIST_REMOVE_HEAD(local_pool, next);
            coroutine_pool_put_global(batch);
        }

        batch = coroutine_pool_batch_new();
        QSLIST_INSERT_HEAD(local_pool, batch, next);
    }

    QSLIST_INSERT_HEAD(&batch->list, co, pool_next);
    batch->size++;
}

Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque)
{
    Coroutine *co = NULL;

    if (IS_ENABLED(CONFIG_COROUTINE_POOL)) {
        co = coroutine_pool_get();
    }

    if (!co) {
        co = qemu_coroutine_new();
    }

    co->entry = entry;
    co->entry_arg = opaque;
    QSIMPLEQ_INIT(&co->co_queue_wakeup);
    return co;
}

static void coroutine_delete(Coroutine *co)
{
    co->caller = NULL;

    if (IS_ENABLED(CONFIG_COROUTINE_POOL)) {
        coroutine_pool_put(co);
    } else {
        qemu_coroutine_delete(co);
    }
}

void qemu_aio_coroutine_enter(AioContext *ctx, Coroutine *co)
{
    QSIMPLEQ_HEAD(, Coroutine) pending = QSIMPLEQ_HEAD_INITIALIZER(pending);
    Coroutine *from = qemu_coroutine_self();

    QSIMPLEQ_INSERT_TAIL(&pending, co, co_queue_next);

    /* Run co and any queued coroutines */
    while (!QSIMPLEQ_EMPTY(&pending)) {
        Coroutine *to = QSIMPLEQ_FIRST(&pending);
        CoroutineAction ret;

        /*
         * Read to before to->scheduled; pairs with qatomic_cmpxchg in
         * qemu_co_sleep(), aio_co_schedule() etc.
         */
        smp_read_barrier_depends();

        const char *scheduled = qatomic_read(&to->scheduled);

        QSIMPLEQ_REMOVE_HEAD(&pending, co_queue_next);

        trace_qemu_aio_coroutine_enter(ctx, from, to, to->entry_arg);

        /* if the Coroutine has already been scheduled, entering it again will
         * cause us to enter it twice, potentially even after the coroutine has
         * been deleted */
        if (scheduled) {
            fprintf(stderr,
                    "%s: Co-routine was already scheduled in '%s'\n",
                    __func__, scheduled);
            abort();
        }

        if (to->caller) {
            fprintf(stderr, "Co-routine re-entered recursively\n");
            abort();
        }

        to->caller = from;
        to->ctx = ctx;

        /* Store to->ctx before anything that stores to.  Matches
         * barrier in aio_co_wake and qemu_co_mutex_wake.
         */
        smp_wmb();

        ret = qemu_coroutine_switch(from, to, COROUTINE_ENTER);

        /* Queued coroutines are run depth-first; previously pending coroutines
         * run after those queued more recently.
         */
        QSIMPLEQ_PREPEND(&pending, &to->co_queue_wakeup);

        switch (ret) {
        case COROUTINE_YIELD:
            break;
        case COROUTINE_TERMINATE:
            assert(!to->locks_held);
            trace_qemu_coroutine_terminate(to);
            coroutine_delete(to);
            break;
        default:
            abort();
        }
    }
}

void qemu_coroutine_enter(Coroutine *co)
{
    qemu_aio_coroutine_enter(qemu_get_current_aio_context(), co);
}

void qemu_coroutine_enter_if_inactive(Coroutine *co)
{
    if (!qemu_coroutine_entered(co)) {
        qemu_coroutine_enter(co);
    }
}

void coroutine_fn qemu_coroutine_yield(void)
{
    Coroutine *self = qemu_coroutine_self();
    Coroutine *to = self->caller;

    trace_qemu_coroutine_yield(self, to);

    if (!to) {
        fprintf(stderr, "Co-routine is yielding to no one\n");
        abort();
    }

    self->caller = NULL;
    qemu_coroutine_switch(self, to, COROUTINE_YIELD);
}

bool qemu_coroutine_entered(Coroutine *co)
{
    return co->caller;
}

AioContext *qemu_coroutine_get_aio_context(Coroutine *co)
{
    return co->ctx;
}

void qemu_coroutine_inc_pool_size(unsigned int additional_pool_size)
{
    QEMU_LOCK_GUARD(&global_pool_lock);
    global_pool_max_size += additional_pool_size;
}

void qemu_coroutine_dec_pool_size(unsigned int removing_pool_size)
{
    QEMU_LOCK_GUARD(&global_pool_lock);
    global_pool_max_size -= removing_pool_size;
}

static unsigned int get_global_pool_hard_max_size(void)
{
#ifdef __linux__
    g_autofree char *contents = NULL;
    int max_map_count;

    /*
     * Linux processes can have up to max_map_count virtual memory areas
     * (VMAs). mmap(2), mprotect(2), etc fail with ENOMEM beyond this limit. We
     * must limit the coroutine pool to a safe size to avoid running out of
     * VMAs.
     */
    if (g_file_get_contents("/proc/sys/vm/max_map_count", &contents, NULL,
                            NULL) &&
        qemu_strtoi(contents, NULL, 10, &max_map_count) == 0) {
        /*
         * This is an upper bound that avoids exceeding max_map_count. Leave a
         * fixed amount for non-coroutine users like library dependencies,
         * vhost-user, etc. Each coroutine takes up 2 VMAs so halve the
         * remaining amount.
         */
        if (max_map_count > 5000) {
            return (max_map_count - 5000) / 2;
        } else {
            /* Disable the global pool but threads still have local pools */
            return 0;
        }
    }
#endif

    return UINT_MAX;
}

static void __attribute__((constructor)) qemu_coroutine_init(void)
{
    qemu_mutex_init(&global_pool_lock);
    global_pool_hard_max_size = get_global_pool_hard_max_size();
}
