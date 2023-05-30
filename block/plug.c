/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Block I/O plugging
 *
 * Copyright Red Hat.
 *
 * This API defers a function call within a blk_io_plug()/blk_io_unplug()
 * section, allowing multiple calls to batch up. This is a performance
 * optimization that is used in the block layer to submit several I/O requests
 * at once instead of individually:
 *
 *   blk_io_plug(); <-- start of plugged region
 *   ...
 *   blk_io_plug_call(my_func, my_obj); <-- deferred my_func(my_obj) call
 *   blk_io_plug_call(my_func, my_obj); <-- another
 *   blk_io_plug_call(my_func, my_obj); <-- another
 *   ...
 *   blk_io_unplug(); <-- end of plugged region, my_func(my_obj) is called once
 *
 * This code is actually generic and not tied to the block layer. If another
 * subsystem needs this functionality, it could be renamed.
 */

#include "qemu/osdep.h"
#include "qemu/coroutine-tls.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "sysemu/block-backend.h"

/* A function call that has been deferred until unplug() */
typedef struct {
    void (*fn)(void *);
    void *opaque;
} UnplugFn;

/* Per-thread state */
typedef struct {
    unsigned count;       /* how many times has plug() been called? */
    GArray *unplug_fns;   /* functions to call at unplug time */
} Plug;

/* Use get_ptr_plug() to fetch this thread-local value */
QEMU_DEFINE_STATIC_CO_TLS(Plug, plug);

/* Called at thread cleanup time */
static void blk_io_plug_atexit(Notifier *n, void *value)
{
    Plug *plug = get_ptr_plug();
    g_array_free(plug->unplug_fns, TRUE);
}

/* This won't involve coroutines, so use __thread */
static __thread Notifier blk_io_plug_atexit_notifier;

/**
 * blk_io_plug_call:
 * @fn: a function pointer to be invoked
 * @opaque: a user-defined argument to @fn()
 *
 * Call @fn(@opaque) immediately if not within a blk_io_plug()/blk_io_unplug()
 * section.
 *
 * Otherwise defer the call until the end of the outermost
 * blk_io_plug()/blk_io_unplug() section in this thread. If the same
 * @fn/@opaque pair has already been deferred, it will only be called once upon
 * blk_io_unplug() so that accumulated calls are batched into a single call.
 *
 * The caller must ensure that @opaque is not freed before @fn() is invoked.
 */
void blk_io_plug_call(void (*fn)(void *), void *opaque)
{
    Plug *plug = get_ptr_plug();

    /* Call immediately if we're not plugged */
    if (plug->count == 0) {
        fn(opaque);
        return;
    }

    GArray *array = plug->unplug_fns;
    if (!array) {
        array = g_array_new(FALSE, FALSE, sizeof(UnplugFn));
        plug->unplug_fns = array;
        blk_io_plug_atexit_notifier.notify = blk_io_plug_atexit;
        qemu_thread_atexit_add(&blk_io_plug_atexit_notifier);
    }

    UnplugFn *fns = (UnplugFn *)array->data;
    UnplugFn new_fn = {
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
 * blk_io_plug: Defer blk_io_plug_call() functions until blk_io_unplug()
 *
 * blk_io_plug/unplug are thread-local operations. This means that multiple
 * threads can simultaneously call plug/unplug, but the caller must ensure that
 * each unplug() is called in the same thread of the matching plug().
 *
 * Nesting is supported. blk_io_plug_call() functions are only called at the
 * outermost blk_io_unplug().
 */
void blk_io_plug(void)
{
    Plug *plug = get_ptr_plug();

    assert(plug->count < UINT32_MAX);

    plug->count++;
}

/**
 * blk_io_unplug: Run any pending blk_io_plug_call() functions
 *
 * There must have been a matching blk_io_plug() call in the same thread prior
 * to this blk_io_unplug() call.
 */
void blk_io_unplug(void)
{
    Plug *plug = get_ptr_plug();

    assert(plug->count > 0);

    if (--plug->count > 0) {
        return;
    }

    GArray *array = plug->unplug_fns;
    if (!array) {
        return;
    }

    UnplugFn *fns = (UnplugFn *)array->data;

    for (guint i = 0; i < array->len; i++) {
        fns[i].fn(fns[i].opaque);
    }

    /*
     * This resets the array without freeing memory so that appending is cheap
     * in the future.
     */
    g_array_set_size(array, 0);
}
