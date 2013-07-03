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
#include "block/coroutine.h"
#include "block/coroutine_int.h"

enum {
    /* Maximum free pool size prevents holding too many freed coroutines */
    POOL_MAX_SIZE = 64,
};

/** Free list to speed up creation */
static QSLIST_HEAD(, Coroutine) pool = QSLIST_HEAD_INITIALIZER(pool);
static unsigned int pool_size;

Coroutine *qemu_coroutine_create(CoroutineEntry *entry)
{
    Coroutine *co;

    co = QSLIST_FIRST(&pool);
    if (co) {
        QSLIST_REMOVE_HEAD(&pool, pool_next);
        pool_size--;
    } else {
        co = qemu_coroutine_new();
    }

    co->entry = entry;
    return co;
}

static void coroutine_delete(Coroutine *co)
{
    if (pool_size < POOL_MAX_SIZE) {
        QSLIST_INSERT_HEAD(&pool, co, pool_next);
        co->caller = NULL;
        pool_size++;
        return;
    }

    qemu_coroutine_delete(co);
}

static void __attribute__((destructor)) coroutine_cleanup(void)
{
    Coroutine *co;
    Coroutine *tmp;

    QSLIST_FOREACH_SAFE(co, &pool, pool_next, tmp) {
        QSLIST_REMOVE_HEAD(&pool, pool_next);
        qemu_coroutine_delete(co);
    }
}

static void coroutine_swap(Coroutine *from, Coroutine *to)
{
    CoroutineAction ret;

    ret = qemu_coroutine_switch(from, to, COROUTINE_YIELD);

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
