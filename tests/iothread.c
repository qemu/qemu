/*
 * Event loop thread implementation for unit tests
 *
 * Copyright Red Hat Inc., 2013, 2016
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/aio.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "iothread.h"

struct IOThread {
    AioContext *ctx;

    QemuThread thread;
    QemuMutex init_done_lock;
    QemuCond init_done_cond;    /* is thread initialization done? */
    bool stopping;
};

static __thread IOThread *my_iothread;

AioContext *qemu_get_current_aio_context(void)
{
    return my_iothread ? my_iothread->ctx : qemu_get_aio_context();
}

static void *iothread_run(void *opaque)
{
    IOThread *iothread = opaque;

    rcu_register_thread();

    my_iothread = iothread;
    qemu_mutex_lock(&iothread->init_done_lock);
    iothread->ctx = aio_context_new(&error_abort);
    qemu_cond_signal(&iothread->init_done_cond);
    qemu_mutex_unlock(&iothread->init_done_lock);

    while (!atomic_read(&iothread->stopping)) {
        aio_poll(iothread->ctx, true);
    }

    rcu_unregister_thread();
    return NULL;
}

static void iothread_stop_bh(void *opaque)
{
    IOThread *iothread = opaque;

    iothread->stopping = true;
}

void iothread_join(IOThread *iothread)
{
    aio_bh_schedule_oneshot(iothread->ctx, iothread_stop_bh, iothread);
    qemu_thread_join(&iothread->thread);
    qemu_cond_destroy(&iothread->init_done_cond);
    qemu_mutex_destroy(&iothread->init_done_lock);
    aio_context_unref(iothread->ctx);
    g_free(iothread);
}

IOThread *iothread_new(void)
{
    IOThread *iothread = g_new0(IOThread, 1);

    qemu_mutex_init(&iothread->init_done_lock);
    qemu_cond_init(&iothread->init_done_cond);
    qemu_thread_create(&iothread->thread, NULL, iothread_run,
                       iothread, QEMU_THREAD_JOINABLE);

    /* Wait for initialization to complete */
    qemu_mutex_lock(&iothread->init_done_lock);
    while (iothread->ctx == NULL) {
        qemu_cond_wait(&iothread->init_done_cond,
                       &iothread->init_done_lock);
    }
    qemu_mutex_unlock(&iothread->init_done_lock);
    return iothread;
}

AioContext *iothread_get_aio_context(IOThread *iothread)
{
    return iothread->ctx;
}
