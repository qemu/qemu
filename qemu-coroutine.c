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

/* XXX Is there a nicer way to disable glibc's stack check for longjmp? */
#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#include <setjmp.h>

#include "trace.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-coroutine.h"
#include "qemu-coroutine-int.h"

static QLIST_HEAD(, Coroutine) pool = QLIST_HEAD_INITIALIZER(&pool);
static __thread Coroutine leader;
static __thread Coroutine *current;

static void qemu_coroutine_terminate(Coroutine *coroutine)
{
    trace_qemu_coroutine_terminate(coroutine);
    QLIST_INSERT_HEAD(&pool, coroutine, pool_next);
    coroutine->caller = NULL;
}

static int coroutine_init(Coroutine *co)
{
    if (!co->initialized) {
        co->initialized = true;
        co->stack_size = 4 << 20;
        co->stack = qemu_malloc(co->stack_size);
    }

    return qemu_coroutine_init_env(co);
}

Coroutine *qemu_coroutine_create(CoroutineEntry *entry)
{
    Coroutine *coroutine;

    coroutine = QLIST_FIRST(&pool);

    if (coroutine) {
        QLIST_REMOVE(coroutine, pool_next);
    } else {
        coroutine = qemu_mallocz(sizeof(*coroutine));
    }

    coroutine_init(coroutine);
    coroutine->entry = entry;

    return coroutine;
}

Coroutine * coroutine_fn qemu_coroutine_self(void)
{
    if (current == NULL) {
        current = &leader;
    }

    return current;
}

bool qemu_in_coroutine(void)
{
    return (qemu_coroutine_self() != &leader);
}

static void *coroutine_swap(Coroutine *from, Coroutine *to, void *opaque)
{
    int ret;

    to->data = opaque;

    ret = setjmp(from->env);
    switch (ret) {
    case COROUTINE_YIELD:
        return from->data;
    case COROUTINE_TERMINATE:
        current = to->caller;
        qemu_coroutine_terminate(to);
        opaque = to->data;
        qemu_free(to->stack);
        qemu_free(to);
        return opaque;
    default:
        /* Switch to called coroutine */
        current = to;
        longjmp(to->env, COROUTINE_YIELD);
        return NULL;
    }
}

void qemu_coroutine_enter(Coroutine *coroutine, void *opaque)
{
    Coroutine *self = qemu_coroutine_self();

    trace_qemu_coroutine_enter(self, coroutine, opaque);

    if (coroutine->caller) {
        fprintf(stderr, "Co-routine re-entered recursively\n");
        abort();
    }

    coroutine->caller = self;
    coroutine_swap(self, coroutine, opaque);
}

void * coroutine_fn qemu_coroutine_yield(void)
{
    Coroutine *self = qemu_coroutine_self();
    Coroutine *to = self->caller;

    trace_qemu_coroutine_yield(self, self->caller);

    if (!to) {
        fprintf(stderr, "Co-routine is yielding to no one\n");
        abort();
    }

    self->caller = NULL;
    return coroutine_swap(self, to, NULL);
}
