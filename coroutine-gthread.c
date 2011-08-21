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
#include "qemu-coroutine-int.h"

typedef struct {
    Coroutine base;
    GThread *thread;
    bool runnable;
    CoroutineAction action;
} CoroutineGThread;

static GCond *coroutine_cond;
static GStaticMutex coroutine_lock = G_STATIC_MUTEX_INIT;
static GStaticPrivate coroutine_key = G_STATIC_PRIVATE_INIT;

static void __attribute__((constructor)) coroutine_init(void)
{
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }

    coroutine_cond = g_cond_new();
}

static void coroutine_wait_runnable_locked(CoroutineGThread *co)
{
    while (!co->runnable) {
        g_cond_wait(coroutine_cond, g_static_mutex_get_mutex(&coroutine_lock));
    }
}

static void coroutine_wait_runnable(CoroutineGThread *co)
{
    g_static_mutex_lock(&coroutine_lock);
    coroutine_wait_runnable_locked(co);
    g_static_mutex_unlock(&coroutine_lock);
}

static gpointer coroutine_thread(gpointer opaque)
{
    CoroutineGThread *co = opaque;

    g_static_private_set(&coroutine_key, co, NULL);
    coroutine_wait_runnable(co);
    co->base.entry(co->base.entry_arg);
    qemu_coroutine_switch(&co->base, co->base.caller, COROUTINE_TERMINATE);
    return NULL;
}

Coroutine *qemu_coroutine_new(void)
{
    CoroutineGThread *co;

    co = g_malloc0(sizeof(*co));
    co->thread = g_thread_create_full(coroutine_thread, co, 0, TRUE, TRUE,
                                      G_THREAD_PRIORITY_NORMAL, NULL);
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

    g_static_mutex_lock(&coroutine_lock);
    from->runnable = false;
    from->action = action;
    to->runnable = true;
    to->action = action;
    g_cond_broadcast(coroutine_cond);

    if (action != COROUTINE_TERMINATE) {
        coroutine_wait_runnable_locked(from);
    }
    g_static_mutex_unlock(&coroutine_lock);
    return from->action;
}

Coroutine *qemu_coroutine_self(void)
{
    CoroutineGThread *co = g_static_private_get(&coroutine_key);

    if (!co) {
        co = g_malloc0(sizeof(*co));
        co->runnable = true;
        g_static_private_set(&coroutine_key, co, (GDestroyNotify)qemu_free);
    }

    return &co->base;
}

bool qemu_in_coroutine(void)
{
    CoroutineGThread *co = g_static_private_get(&coroutine_key);

    return co && co->base.caller;
}
