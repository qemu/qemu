/*
 * ucontext coroutine initialization code
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2011  Kevin Wolf <kwolf@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* XXX Is there a nicer way to disable glibc's stack check for longjmp? */
#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <pthread.h>
#include <ucontext.h>
#include "qemu-common.h"
#include "qemu-coroutine-int.h"

enum {
    /* Maximum free pool size prevents holding too many freed coroutines */
    POOL_MAX_SIZE = 64,
};

typedef struct {
    Coroutine base;
    void *stack;
    jmp_buf env;
} CoroutineUContext;

/**
 * Per-thread coroutine bookkeeping
 */
typedef struct {
    /** Currently executing coroutine */
    Coroutine *current;

    /** Free list to speed up creation */
    QLIST_HEAD(, Coroutine) pool;
    unsigned int pool_size;

    /** The default coroutine */
    CoroutineUContext leader;
} CoroutineThreadState;

static pthread_key_t thread_state_key;

/*
 * va_args to makecontext() must be type 'int', so passing
 * the pointer we need may require several int args. This
 * union is a quick hack to let us do that
 */
union cc_arg {
    void *p;
    int i[2];
};

static CoroutineThreadState *coroutine_get_thread_state(void)
{
    CoroutineThreadState *s = pthread_getspecific(thread_state_key);

    if (!s) {
        s = qemu_mallocz(sizeof(*s));
        s->current = &s->leader.base;
        QLIST_INIT(&s->pool);
        pthread_setspecific(thread_state_key, s);
    }
    return s;
}

static void qemu_coroutine_thread_cleanup(void *opaque)
{
    CoroutineThreadState *s = opaque;
    Coroutine *co;
    Coroutine *tmp;

    QLIST_FOREACH_SAFE(co, &s->pool, pool_next, tmp) {
        qemu_free(DO_UPCAST(CoroutineUContext, base, co)->stack);
        qemu_free(co);
    }
    qemu_free(s);
}

static void __attribute__((constructor)) coroutine_init(void)
{
    int ret;

    ret = pthread_key_create(&thread_state_key, qemu_coroutine_thread_cleanup);
    if (ret != 0) {
        fprintf(stderr, "unable to create leader key: %s\n", strerror(errno));
        abort();
    }
}

static void coroutine_trampoline(int i0, int i1)
{
    union cc_arg arg;
    CoroutineUContext *self;
    Coroutine *co;

    arg.i[0] = i0;
    arg.i[1] = i1;
    self = arg.p;
    co = &self->base;

    /* Initialize longjmp environment and switch back the caller */
    if (!setjmp(self->env)) {
        longjmp(*(jmp_buf *)co->entry_arg, 1);
    }

    while (true) {
        co->entry(co->entry_arg);
        qemu_coroutine_switch(co, co->caller, COROUTINE_TERMINATE);
    }
}

static Coroutine *coroutine_new(void)
{
    const size_t stack_size = 1 << 20;
    CoroutineUContext *co;
    ucontext_t old_uc, uc;
    jmp_buf old_env;
    union cc_arg arg;

    /* The ucontext functions preserve signal masks which incurs a system call
     * overhead.  setjmp()/longjmp() does not preserve signal masks but only
     * works on the current stack.  Since we need a way to create and switch to
     * a new stack, use the ucontext functions for that but setjmp()/longjmp()
     * for everything else.
     */

    if (getcontext(&uc) == -1) {
        abort();
    }

    co = qemu_mallocz(sizeof(*co));
    co->stack = qemu_malloc(stack_size);
    co->base.entry_arg = &old_env; /* stash away our jmp_buf */

    uc.uc_link = &old_uc;
    uc.uc_stack.ss_sp = co->stack;
    uc.uc_stack.ss_size = stack_size;
    uc.uc_stack.ss_flags = 0;

    arg.p = co;

    makecontext(&uc, (void (*)(void))coroutine_trampoline,
                2, arg.i[0], arg.i[1]);

    /* swapcontext() in, longjmp() back out */
    if (!setjmp(old_env)) {
        swapcontext(&old_uc, &uc);
    }
    return &co->base;
}

Coroutine *qemu_coroutine_new(void)
{
    CoroutineThreadState *s = coroutine_get_thread_state();
    Coroutine *co;

    co = QLIST_FIRST(&s->pool);
    if (co) {
        QLIST_REMOVE(co, pool_next);
        s->pool_size--;
    } else {
        co = coroutine_new();
    }
    return co;
}

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineThreadState *s = coroutine_get_thread_state();
    CoroutineUContext *co = DO_UPCAST(CoroutineUContext, base, co_);

    if (s->pool_size < POOL_MAX_SIZE) {
        QLIST_INSERT_HEAD(&s->pool, &co->base, pool_next);
        co->base.caller = NULL;
        s->pool_size++;
        return;
    }

    qemu_free(co->stack);
    qemu_free(co);
}

CoroutineAction qemu_coroutine_switch(Coroutine *from_, Coroutine *to_,
                                      CoroutineAction action)
{
    CoroutineUContext *from = DO_UPCAST(CoroutineUContext, base, from_);
    CoroutineUContext *to = DO_UPCAST(CoroutineUContext, base, to_);
    CoroutineThreadState *s = coroutine_get_thread_state();
    int ret;

    s->current = to_;

    ret = setjmp(from->env);
    if (ret == 0) {
        longjmp(to->env, action);
    }
    return ret;
}

Coroutine *qemu_coroutine_self(void)
{
    CoroutineThreadState *s = coroutine_get_thread_state();

    return s->current;
}

bool qemu_in_coroutine(void)
{
    CoroutineThreadState *s = pthread_getspecific(thread_state_key);

    return s && s->current->caller;
}
