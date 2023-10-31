/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Deferred calls
 *
 * Copyright Red Hat.
 *
 * This API defers a function call within a defer_call_begin()/defer_call_end()
 * section, allowing multiple calls to batch up. This is a performance
 * optimization that is used in the block layer to submit several I/O requests
 * at once instead of individually:
 *
 *   defer_call_begin(); <-- start of section
 *   ...
 *   defer_call(my_func, my_obj); <-- deferred my_func(my_obj) call
 *   defer_call(my_func, my_obj); <-- another
 *   defer_call(my_func, my_obj); <-- another
 *   ...
 *   defer_call_end(); <-- end of section, my_func(my_obj) is called once
 */

#include "qemu/osdep.h"
#include "qemu/coroutine-tls.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "qemu/defer-call.h"

/* A function call that has been deferred until defer_call_end() */
typedef struct {
    void (*fn)(void *);
    void *opaque;
} DeferredCall;

/* Per-thread state */
typedef struct {
    unsigned nesting_level;
    GArray *deferred_call_array;
} DeferCallThreadState;

/* Use get_ptr_defer_call_thread_state() to fetch this thread-local value */
QEMU_DEFINE_STATIC_CO_TLS(DeferCallThreadState, defer_call_thread_state);

/* Called at thread cleanup time */
static void defer_call_atexit(Notifier *n, void *value)
{
    DeferCallThreadState *thread_state = get_ptr_defer_call_thread_state();
    g_array_free(thread_state->deferred_call_array, TRUE);
}

/* This won't involve coroutines, so use __thread */
static __thread Notifier defer_call_atexit_notifier;

/**
 * defer_call:
 * @fn: a function pointer to be invoked
 * @opaque: a user-defined argument to @fn()
 *
 * Call @fn(@opaque) immediately if not within a
 * defer_call_begin()/defer_call_end() section.
 *
 * Otherwise defer the call until the end of the outermost
 * defer_call_begin()/defer_call_end() section in this thread. If the same
 * @fn/@opaque pair has already been deferred, it will only be called once upon
 * defer_call_end() so that accumulated calls are batched into a single call.
 *
 * The caller must ensure that @opaque is not freed before @fn() is invoked.
 */
void defer_call(void (*fn)(void *), void *opaque)
{
    DeferCallThreadState *thread_state = get_ptr_defer_call_thread_state();

    /* Call immediately if we're not deferring calls */
    if (thread_state->nesting_level == 0) {
        fn(opaque);
        return;
    }

    GArray *array = thread_state->deferred_call_array;
    if (!array) {
        array = g_array_new(FALSE, FALSE, sizeof(DeferredCall));
        thread_state->deferred_call_array = array;
        defer_call_atexit_notifier.notify = defer_call_atexit;
        qemu_thread_atexit_add(&defer_call_atexit_notifier);
    }

    DeferredCall *fns = (DeferredCall *)array->data;
    DeferredCall new_fn = {
        .fn = fn,
        .opaque = opaque,
    };

    /*
     * There won't be many, so do a linear search. If this becomes a bottleneck
     * then a binary search (glib 2.62+) or different data structure could be
     * used.
     */
    for (guint i = 0; i < array->len; i++) {
        if (memcmp(&fns[i], &new_fn, sizeof(new_fn)) == 0) {
            return; /* already exists */
        }
    }

    g_array_append_val(array, new_fn);
}

/**
 * defer_call_begin: Defer defer_call() functions until defer_call_end()
 *
 * defer_call_begin() and defer_call_end() are thread-local operations. The
 * caller must ensure that each defer_call_begin() has a matching
 * defer_call_end() in the same thread.
 *
 * Nesting is supported. defer_call() functions are only called at the
 * outermost defer_call_end().
 */
void defer_call_begin(void)
{
    DeferCallThreadState *thread_state = get_ptr_defer_call_thread_state();

    assert(thread_state->nesting_level < UINT32_MAX);

    thread_state->nesting_level++;
}

/**
 * defer_call_end: Run any pending defer_call() functions
 *
 * There must have been a matching defer_call_begin() call in the same thread
 * prior to this defer_call_end() call.
 */
void defer_call_end(void)
{
    DeferCallThreadState *thread_state = get_ptr_defer_call_thread_state();

    assert(thread_state->nesting_level > 0);

    if (--thread_state->nesting_level > 0) {
        return;
    }

    GArray *array = thread_state->deferred_call_array;
    if (!array) {
        return;
    }

    DeferredCall *fns = (DeferredCall *)array->data;

    for (guint i = 0; i < array->len; i++) {
        fns[i].fn(fns[i].opaque);
    }

    /*
     * This resets the array without freeing memory so that appending is cheap
     * in the future.
     */
    g_array_set_size(array, 0);
}
