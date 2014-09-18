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

#include "trace.h"
#include "qemu-common.h"
#include "qemu/thread.h"
#include "block/coroutine.h"
#include "block/coroutine_int.h"

enum {
    POOL_DEFAULT_SIZE = 64,
};

/** Free list to speed up creation */
static QemuMutex pool_lock;
static QSLIST_HEAD(, Coroutine) pool = QSLIST_HEAD_INITIALIZER(pool);
static unsigned int pool_size;
static unsigned int pool_max_size = POOL_DEFAULT_SIZE;

Coroutine *qemu_coroutine_create(CoroutineEntry *entry)
{
    Coroutine *co = NULL;

    if (CONFIG_COROUTINE_POOL) {
        qemu_mutex_lock(&pool_lock);
        co = QSLIST_FIRST(&pool);
        if (co) {
            QSLIST_REMOVE_HEAD(&pool, pool_next);
            pool_size--;
        }
        qemu_mutex_unlock(&pool_lock);
    }

    if (!co) {
        co = qemu_coroutine_new();
    }

    co->entry = entry;
    QTAILQ_INIT(&co->co_queue_wakeup);
    return co;
}

static void coroutine_delete(Coroutine *co)
{
    if (CONFIG_COROUTINE_POOL) {
        qemu_mutex_lock(&pool_lock);
        if (pool_size < pool_max_size) {
            QSLIST_INSERT_HEAD(&pool, co, pool_next);
            co->caller = NULL;
            pool_size++;
            qemu_mutex_unlock(&pool_lock);
            return;
        }
        qemu_mutex_unlock(&pool_lock);
    }

    qemu_coroutine_delete(co);
}

static void __attribute__((constructor)) coroutine_pool_init(void)
{
    qemu_mutex_init(&pool_lock);
}

static void __attribute__((destructor)) coroutine_pool_cleanup(void)
{
    Coroutine *co;
    Coroutine *tmp;

    QSLIST_FOREACH_SAFE(co, &pool, pool_next, tmp) {
        QSLIST_REMOVE_HEAD(&pool, pool_next);
        qemu_coroutine_delete(co);
    }

    qemu_mutex_destroy(&pool_lock);
}

static void coroutine_swap(Coroutine *from, Coroutine *to)
{
    CoroutineAction ret;

    ret = qemu_coroutine_switch(from, to, COROUTINE_YIELD);

    qemu_co_queue_run_restart(to);

    switch (ret) {
    case COROUTINE_YIELD:
        return;
    case COROUTINE_TERMINATE:
        trace_qemu_coroutine_terminate(to);
        coroutine_delete(to);
        return;
    default:
        abort();
    }
}

void qemu_coroutine_enter(Coroutine *co, void *opaque)
{
    Coroutine *self = qemu_coroutine_self();

    trace_qemu_coroutine_enter(self, co, opaque);

    if (co->caller) {
        fprintf(stderr, "Co-routine re-entered recursively\n");
        abort();
    }

    co->caller = self;
    co->entry_arg = opaque;
    coroutine_swap(self, co);
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
    coroutine_swap(self, to);
}

void qemu_coroutine_adjust_pool_size(int n)
{
    qemu_mutex_lock(&pool_lock);

    pool_max_size += n;

    /* Callers should never take away more than they added */
    assert(pool_max_size >= POOL_DEFAULT_SIZE);

    /* Trim oversized pool down to new max */
    while (pool_size > pool_max_size) {
        Coroutine *co = QSLIST_FIRST(&pool);
        QSLIST_REMOVE_HEAD(&pool, pool_next);
        pool_size--;
        qemu_coroutine_delete(co);
    }

    qemu_mutex_unlock(&pool_lock);
}
