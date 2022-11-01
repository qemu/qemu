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
#include "qemu/coroutine.h"
#include "qemu/coroutine_int.h"
#include "qemu/coroutine-tls.h"
#include "block/aio.h"

/**
 * The minimal batch size is always 64, coroutines from the release_pool are
 * reused as soon as there are 64 coroutines in it. The maximum pool size starts
 * with 64 and is increased on demand so that coroutines are not deleted even if
 * they are not immediately reused.
 */
enum {
    POOL_MIN_BATCH_SIZE = 64,
    POOL_INITIAL_MAX_SIZE = 64,
};

/** Free list to speed up creation */
static QSLIST_HEAD(, Coroutine) release_pool = QSLIST_HEAD_INITIALIZER(pool);
static unsigned int pool_max_size = POOL_INITIAL_MAX_SIZE;
static unsigned int release_pool_size;

typedef QSLIST_HEAD(, Coroutine) CoroutineQSList;
QEMU_DEFINE_STATIC_CO_TLS(CoroutineQSList, alloc_pool);
QEMU_DEFINE_STATIC_CO_TLS(unsigned int, alloc_pool_size);
QEMU_DEFINE_STATIC_CO_TLS(Notifier, coroutine_pool_cleanup_notifier);

static void coroutine_pool_cleanup(Notifier *n, void *value)
{
    Coroutine *co;
    Coroutine *tmp;
    CoroutineQSList *alloc_pool = get_ptr_alloc_pool();

    QSLIST_FOREACH_SAFE(co, alloc_pool, pool_next, tmp) {
        QSLIST_REMOVE_HEAD(alloc_pool, pool_next);
        qemu_coroutine_delete(co);
    }
}

Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque)
{
    Coroutine *co = NULL;

    if (CONFIG_COROUTINE_POOL) {
        CoroutineQSList *alloc_pool = get_ptr_alloc_pool();

        co = QSLIST_FIRST(alloc_pool);
        if (!co) {
            if (release_pool_size > POOL_MIN_BATCH_SIZE) {
                /* Slow path; a good place to register the destructor, too.  */
                Notifier *notifier = get_ptr_coroutine_pool_cleanup_notifier();
                if (!notifier->notify) {
                    notifier->notify = coroutine_pool_cleanup;
                    qemu_thread_atexit_add(notifier);
                }

                /* This is not exact; there could be a little skew between
                 * release_pool_size and the actual size of release_pool.  But
                 * it is just a heuristic, it does not need to be perfect.
                 */
                set_alloc_pool_size(qatomic_xchg(&release_pool_size, 0));
                QSLIST_MOVE_ATOMIC(alloc_pool, &release_pool);
                co = QSLIST_FIRST(alloc_pool);
            }
        }
        if (co) {
            QSLIST_REMOVE_HEAD(alloc_pool, pool_next);
            set_alloc_pool_size(get_alloc_pool_size() - 1);
        }
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

    if (CONFIG_COROUTINE_POOL) {
        if (release_pool_size < qatomic_read(&pool_max_size) * 2) {
            QSLIST_INSERT_HEAD_ATOMIC(&release_pool, co, pool_next);
            qatomic_inc(&release_pool_size);
            return;
        }
        if (get_alloc_pool_size() < qatomic_read(&pool_max_size)) {
            QSLIST_INSERT_HEAD(get_ptr_alloc_pool(), co, pool_next);
            set_alloc_pool_size(get_alloc_pool_size() + 1);
            return;
        }
    }

    qemu_coroutine_delete(co);
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

        /* Cannot rely on the read barrier for to in aio_co_wake(), as there are
         * callers outside of aio_co_wake() */
        const char *scheduled = qatomic_mb_read(&to->scheduled);

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
    qatomic_add(&pool_max_size, additional_pool_size);
}

void qemu_coroutine_dec_pool_size(unsigned int removing_pool_size)
{
    qatomic_sub(&pool_max_size, removing_pool_size);
}
