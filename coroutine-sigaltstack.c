/*
 * sigaltstack coroutine initialization code
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2011  Kevin Wolf <kwolf@redhat.com>
 * Copyright (C) 2012  Alex Barcelo <abarcelo@ac.upc.edu>
** This file is partly based on pth_mctx.c, from the GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
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
#include <signal.h>
#include "qemu-common.h"
#include "qemu-coroutine-int.h"

enum {
    /* Maximum free pool size prevents holding too many freed coroutines */
    POOL_MAX_SIZE = 64,
};

/** Free list to speed up creation */
static QSLIST_HEAD(, Coroutine) pool = QSLIST_HEAD_INITIALIZER(pool);
static unsigned int pool_size;

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

    /** The default coroutine */
    CoroutineUContext leader;

    /** Information for the signal handler (trampoline) */
    jmp_buf tr_reenter;
    volatile sig_atomic_t tr_called;
    void *tr_handler;
} CoroutineThreadState;

static pthread_key_t thread_state_key;

static CoroutineThreadState *coroutine_get_thread_state(void)
{
    CoroutineThreadState *s = pthread_getspecific(thread_state_key);

    if (!s) {
        s = g_malloc0(sizeof(*s));
        s->current = &s->leader.base;
        pthread_setspecific(thread_state_key, s);
    }
    return s;
}

static void qemu_coroutine_thread_cleanup(void *opaque)
{
    CoroutineThreadState *s = opaque;

    g_free(s);
}

static void __attribute__((destructor)) coroutine_cleanup(void)
{
    Coroutine *co;
    Coroutine *tmp;

    QSLIST_FOREACH_SAFE(co, &pool, pool_next, tmp) {
        g_free(DO_UPCAST(CoroutineUContext, base, co)->stack);
        g_free(co);
    }
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

/* "boot" function
 * This is what starts the coroutine, is called from the trampoline
 * (from the signal handler when it is not signal handling, read ahead
 * for more information).
 */
static void coroutine_bootstrap(CoroutineUContext *self, Coroutine *co)
{
    /* Initialize longjmp environment and switch back the caller */
    if (!setjmp(self->env)) {
        longjmp(*(jmp_buf *)co->entry_arg, 1);
    }

    while (true) {
        co->entry(co->entry_arg);
        qemu_coroutine_switch(co, co->caller, COROUTINE_TERMINATE);
    }
}

/*
 * This is used as the signal handler. This is called with the brand new stack
 * (thanks to sigaltstack). We have to return, given that this is a signal
 * handler and the sigmask and some other things are changed.
 */
static void coroutine_trampoline(int signal)
{
    CoroutineUContext *self;
    Coroutine *co;
    CoroutineThreadState *coTS;

    /* Get the thread specific information */
    coTS = coroutine_get_thread_state();
    self = coTS->tr_handler;
    coTS->tr_called = 1;
    co = &self->base;

    /*
     * Here we have to do a bit of a ping pong between the caller, given that
     * this is a signal handler and we have to do a return "soon". Then the
     * caller can reestablish everything and do a longjmp here again.
     */
    if (!setjmp(coTS->tr_reenter)) {
        return;
    }

    /*
     * Ok, the caller has longjmp'ed back to us, so now prepare
     * us for the real machine state switching. We have to jump
     * into another function here to get a new stack context for
     * the auto variables (which have to be auto-variables
     * because the start of the thread happens later). Else with
     * PIC (i.e. Position Independent Code which is used when PTH
     * is built as a shared library) most platforms would
     * horrible core dump as experience showed.
     */
    coroutine_bootstrap(self, co);
}

static Coroutine *coroutine_new(void)
{
    const size_t stack_size = 1 << 20;
    CoroutineUContext *co;
    CoroutineThreadState *coTS;
    struct sigaction sa;
    struct sigaction osa;
    struct sigaltstack ss;
    struct sigaltstack oss;
    sigset_t sigs;
    sigset_t osigs;
    jmp_buf old_env;

    /* The way to manipulate stack is with the sigaltstack function. We
     * prepare a stack, with it delivering a signal to ourselves and then
     * put setjmp/longjmp where needed.
     * This has been done keeping coroutine-ucontext as a model and with the
     * pth ideas (GNU Portable Threads). See coroutine-ucontext for the basics
     * of the coroutines and see pth_mctx.c (from the pth project) for the
     * sigaltstack way of manipulating stacks.
     */

    co = g_malloc0(sizeof(*co));
    co->stack = g_malloc(stack_size);
    co->base.entry_arg = &old_env; /* stash away our jmp_buf */

    coTS = coroutine_get_thread_state();
    coTS->tr_handler = co;

    /*
     * Preserve the SIGUSR2 signal state, block SIGUSR2,
     * and establish our signal handler. The signal will
     * later transfer control onto the signal stack.
     */
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &sigs, &osigs);
    sa.sa_handler = coroutine_trampoline;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_ONSTACK;
    if (sigaction(SIGUSR2, &sa, &osa) != 0) {
        abort();
    }

    /*
     * Set the new stack.
     */
    ss.ss_sp = co->stack;
    ss.ss_size = stack_size;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, &oss) < 0) {
        abort();
    }

    /*
     * Now transfer control onto the signal stack and set it up.
     * It will return immediately via "return" after the setjmp()
     * was performed. Be careful here with race conditions.  The
     * signal can be delivered the first time sigsuspend() is
     * called.
     */
    coTS->tr_called = 0;
    kill(getpid(), SIGUSR2);
    sigfillset(&sigs);
    sigdelset(&sigs, SIGUSR2);
    while (!coTS->tr_called) {
        sigsuspend(&sigs);
    }

    /*
     * Inform the system that we are back off the signal stack by
     * removing the alternative signal stack. Be careful here: It
     * first has to be disabled, before it can be removed.
     */
    sigaltstack(NULL, &ss);
    ss.ss_flags = SS_DISABLE;
    if (sigaltstack(&ss, NULL) < 0) {
        abort();
    }
    sigaltstack(NULL, &ss);
    if (!(oss.ss_flags & SS_DISABLE)) {
        sigaltstack(&oss, NULL);
    }

    /*
     * Restore the old SIGUSR2 signal handler and mask
     */
    sigaction(SIGUSR2, &osa, NULL);
    pthread_sigmask(SIG_SETMASK, &osigs, NULL);

    /*
     * Now enter the trampoline again, but this time not as a signal
     * handler. Instead we jump into it directly. The functionally
     * redundant ping-pong pointer arithmentic is neccessary to avoid
     * type-conversion warnings related to the `volatile' qualifier and
     * the fact that `jmp_buf' usually is an array type.
     */
    if (!setjmp(old_env)) {
        longjmp(coTS->tr_reenter, 1);
    }

    /*
     * Ok, we returned again, so now we're finished
     */

    return &co->base;
}

Coroutine *qemu_coroutine_new(void)
{
    Coroutine *co;

    co = QSLIST_FIRST(&pool);
    if (co) {
        QSLIST_REMOVE_HEAD(&pool, pool_next);
        pool_size--;
    } else {
        co = coroutine_new();
    }
    return co;
}

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineUContext *co = DO_UPCAST(CoroutineUContext, base, co_);

    if (pool_size < POOL_MAX_SIZE) {
        QSLIST_INSERT_HEAD(&pool, &co->base, pool_next);
        co->base.caller = NULL;
        pool_size++;
        return;
    }

    g_free(co->stack);
    g_free(co);
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

