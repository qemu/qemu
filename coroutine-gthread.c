/*
 * GThread coroutine initialization code
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2011  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
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

#include <glib.h>
#include "qemu-common.h"
#include "block/coroutine_int.h"

typedef struct {
    Coroutine base;
    GThread *thread;
    bool runnable;
    bool free_on_thread_exit;
    CoroutineAction action;
} CoroutineGThread;

static CompatGMutex coroutine_lock;
static CompatGCond coroutine_cond;

/* GLib 2.31 and beyond deprecated various parts of the thread API,
 * but the new interfaces are not available in older GLib versions
 * so we have to cope with both.
 */
#if GLIB_CHECK_VERSION(2, 31, 0)
/* Awkwardly, the GPrivate API doesn't provide a way to update the
 * GDestroyNotify handler for the coroutine key dynamically. So instead
 * we track whether or not the CoroutineGThread should be freed on
 * thread exit / coroutine key update using the free_on_thread_exit
 * field.
 */
static void coroutine_destroy_notify(gpointer data)
{
    CoroutineGThread *co = data;
    if (co && co->free_on_thread_exit) {
        g_free(co);
    }
}

static GPrivate coroutine_key = G_PRIVATE_INIT(coroutine_destroy_notify);

static inline CoroutineGThread *get_coroutine_key(void)
{
    return g_private_get(&coroutine_key);
}

static inline void set_coroutine_key(CoroutineGThread *co,
                                     bool free_on_thread_exit)
{
    /* Unlike g_static_private_set() this does not call the GDestroyNotify
     * if the previous value of the key was NULL. Fortunately we only need
     * the GDestroyNotify in the non-NULL key case.
     */
    co->free_on_thread_exit = free_on_thread_exit;
    g_private_replace(&coroutine_key, co);
}

static inline GThread *create_thread(GThreadFunc func, gpointer data)
{
    return g_thread_new("coroutine", func, data);
}

#else

/* Handle older GLib versions */

static GStaticPrivate coroutine_key = G_STATIC_PRIVATE_INIT;

static inline CoroutineGThread *get_coroutine_key(void)
{
    return g_static_private_get(&coroutine_key);
}

static inline void set_coroutine_key(CoroutineGThread *co,
                                     bool free_on_thread_exit)
{
    g_static_private_set(&coroutine_key, co,
                         free_on_thread_exit ? (GDestroyNotify)g_free : NULL);
}

static inline GThread *create_thread(GThreadFunc func, gpointer data)
{
    return g_thread_create_full(func, data, 0, TRUE, TRUE,
                                G_THREAD_PRIORITY_NORMAL, NULL);
}

#endif


static void __attribute__((constructor)) coroutine_init(void)
{
#if !GLIB_CHECK_VERSION(2, 31, 0)
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }
#endif
}

static void coroutine_wait_runnable_locked(CoroutineGThread *co)
{
    while (!co->runnable) {
        g_cond_wait(&coroutine_cond, &coroutine_lock);
    }
}

static void coroutine_wait_runnable(CoroutineGThread *co)
{
    g_mutex_lock(&coroutine_lock);
    coroutine_wait_runnable_locked(co);
    g_mutex_unlock(&coroutine_lock);
}

static gpointer coroutine_thread(gpointer opaque)
{
    CoroutineGThread *co = opaque;

    set_coroutine_key(co, false);
    coroutine_wait_runnable(co);
    co->base.entry(co->base.entry_arg);
    qemu_coroutine_switch(&co->base, co->base.caller, COROUTINE_TERMINATE);
    return NULL;
}

Coroutine *qemu_coroutine_new(void)
{
    CoroutineGThread *co;

    co = g_malloc0(sizeof(*co));
    co->thread = create_thread(coroutine_thread, co);
    if (!co->thread) {
        g_free(co);
        return NULL;
    }
    return &co->base;
}

void qemu_coroutine_delete(Coroutine *co_)
{
    CoroutineGThread *co = DO_UPCAST(CoroutineGThread, base, co_);

    g_thread_join(co->thread);
    g_free(co);
}

CoroutineAction qemu_coroutine_switch(Coroutine *from_,
                                      Coroutine *to_,
                                      CoroutineAction action)
{
    CoroutineGThread *from = DO_UPCAST(CoroutineGThread, base, from_);
    CoroutineGThread *to = DO_UPCAST(CoroutineGThread, base, to_);

    g_mutex_lock(&coroutine_lock);
    from->runnable = false;
    from->action = action;
    to->runnable = true;
    to->action = action;
    g_cond_broadcast(&coroutine_cond);

    if (action != COROUTINE_TERMINATE) {
        coroutine_wait_runnable_locked(from);
    }
    g_mutex_unlock(&coroutine_lock);
    return from->action;
}

Coroutine *qemu_coroutine_self(void)
{
    CoroutineGThread *co = get_coroutine_key();
    if (!co) {
        co = g_malloc0(sizeof(*co));
        co->runnable = true;
        set_coroutine_key(co, true);
    }

    return &co->base;
}

bool qemu_in_coroutine(void)
{
    CoroutineGThread *co = get_coroutine_key();

    return co && co->base.caller;
}
