/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Harsh Prateek Bora <harsh@linux.vnet.ibm.com>
 *  Venkateswararao Jujjuri(JV) <jvrao@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "fsdev/qemu-fsdev.h"
#include "qemu/thread.h"
#include "qemu/event_notifier.h"
#include "block/coroutine.h"
#include "virtio-9p-coth.h"

/* v9fs glib thread pool */
static V9fsThPool v9fs_pool;

void co_run_in_worker_bh(void *opaque)
{
    Coroutine *co = opaque;
    g_thread_pool_push(v9fs_pool.pool, co, NULL);
}

static void v9fs_qemu_process_req_done(EventNotifier *e)
{
    Coroutine *co;

    event_notifier_test_and_clear(e);

    while ((co = g_async_queue_try_pop(v9fs_pool.completed)) != NULL) {
        qemu_coroutine_enter(co, NULL);
    }
}

static void v9fs_thread_routine(gpointer data, gpointer user_data)
{
    Coroutine *co = data;

    qemu_coroutine_enter(co, NULL);

    g_async_queue_push(v9fs_pool.completed, co);

    event_notifier_set(&v9fs_pool.e);
}

int v9fs_init_worker_threads(void)
{
    int ret = 0;
    V9fsThPool *p = &v9fs_pool;
    sigset_t set, oldset;

    sigfillset(&set);
    /* Leave signal handling to the iothread.  */
    pthread_sigmask(SIG_SETMASK, &set, &oldset);

    p->pool = g_thread_pool_new(v9fs_thread_routine, p, -1, FALSE, NULL);
    if (!p->pool) {
        ret = -1;
        goto err_out;
    }
    p->completed = g_async_queue_new();
    if (!p->completed) {
        /*
         * We are going to terminate.
         * So don't worry about cleanup
         */
        ret = -1;
        goto err_out;
    }
    event_notifier_init(&p->e, 0);

    event_notifier_set_handler(&p->e, v9fs_qemu_process_req_done);
err_out:
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    return ret;
}
