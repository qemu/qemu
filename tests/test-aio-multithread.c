/*
 * AioContext multithreading tests
 *
 * Copyright Red Hat, Inc. 2016
 *
 * Authors:
 *  Paolo Bonzini    <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "block/aio.h"
#include "qapi/error.h"
#include "qemu/coroutine.h"
#include "qemu/thread.h"
#include "qemu/error-report.h"
#include "iothread.h"

/* AioContext management */

#define NUM_CONTEXTS 5

static IOThread *threads[NUM_CONTEXTS];
static AioContext *ctx[NUM_CONTEXTS];
static __thread int id = -1;

static QemuEvent done_event;

/* Run a function synchronously on a remote iothread. */

typedef struct CtxRunData {
    QEMUBHFunc *cb;
    void *arg;
} CtxRunData;

static void ctx_run_bh_cb(void *opaque)
{
    CtxRunData *data = opaque;

    data->cb(data->arg);
    qemu_event_set(&done_event);
}

static void ctx_run(int i, QEMUBHFunc *cb, void *opaque)
{
    CtxRunData data = {
        .cb = cb,
        .arg = opaque
    };

    qemu_event_reset(&done_event);
    aio_bh_schedule_oneshot(ctx[i], ctx_run_bh_cb, &data);
    qemu_event_wait(&done_event);
}

/* Starting the iothreads. */

static void set_id_cb(void *opaque)
{
    int *i = opaque;

    id = *i;
}

static void create_aio_contexts(void)
{
    int i;

    for (i = 0; i < NUM_CONTEXTS; i++) {
        threads[i] = iothread_new();
        ctx[i] = iothread_get_aio_context(threads[i]);
    }

    qemu_event_init(&done_event, false);
    for (i = 0; i < NUM_CONTEXTS; i++) {
        ctx_run(i, set_id_cb, &i);
    }
}

/* Stopping the iothreads. */

static void join_aio_contexts(void)
{
    int i;

    for (i = 0; i < NUM_CONTEXTS; i++) {
        aio_context_ref(ctx[i]);
    }
    for (i = 0; i < NUM_CONTEXTS; i++) {
        iothread_join(threads[i]);
    }
    for (i = 0; i < NUM_CONTEXTS; i++) {
        aio_context_unref(ctx[i]);
    }
    qemu_event_destroy(&done_event);
}

/* Basic test for the stuff above. */

static void test_lifecycle(void)
{
    create_aio_contexts();
    join_aio_contexts();
}

/* aio_co_schedule test.  */

static Coroutine *to_schedule[NUM_CONTEXTS];

static bool now_stopping;

static int count_retry;
static int count_here;
static int count_other;

static bool schedule_next(int n)
{
    Coroutine *co;

    co = atomic_xchg(&to_schedule[n], NULL);
    if (!co) {
        atomic_inc(&count_retry);
        return false;
    }

    if (n == id) {
        atomic_inc(&count_here);
    } else {
        atomic_inc(&count_other);
    }

    aio_co_schedule(ctx[n], co);
    return true;
}

static void finish_cb(void *opaque)
{
    schedule_next(id);
}

static coroutine_fn void test_multi_co_schedule_entry(void *opaque)
{
    g_assert(to_schedule[id] == NULL);
    atomic_mb_set(&to_schedule[id], qemu_coroutine_self());

    while (!atomic_mb_read(&now_stopping)) {
        int n;

        n = g_test_rand_int_range(0, NUM_CONTEXTS);
        schedule_next(n);
        qemu_coroutine_yield();

        g_assert(to_schedule[id] == NULL);
        atomic_mb_set(&to_schedule[id], qemu_coroutine_self());
    }
}


static void test_multi_co_schedule(int seconds)
{
    int i;

    count_here = count_other = count_retry = 0;
    now_stopping = false;

    create_aio_contexts();
    for (i = 0; i < NUM_CONTEXTS; i++) {
        Coroutine *co1 = qemu_coroutine_create(test_multi_co_schedule_entry, NULL);
        aio_co_schedule(ctx[i], co1);
    }

    g_usleep(seconds * 1000000);

    atomic_mb_set(&now_stopping, true);
    for (i = 0; i < NUM_CONTEXTS; i++) {
        ctx_run(i, finish_cb, NULL);
        to_schedule[i] = NULL;
    }

    join_aio_contexts();
    g_test_message("scheduled %d, queued %d, retry %d, total %d\n",
                  count_other, count_here, count_retry,
                  count_here + count_other + count_retry);
}

static void test_multi_co_schedule_1(void)
{
    test_multi_co_schedule(1);
}

static void test_multi_co_schedule_10(void)
{
    test_multi_co_schedule(10);
}

/* End of tests.  */

int main(int argc, char **argv)
{
    init_clocks();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/aio/multi/lifecycle", test_lifecycle);
    if (g_test_quick()) {
        g_test_add_func("/aio/multi/schedule", test_multi_co_schedule_1);
    } else {
        g_test_add_func("/aio/multi/schedule", test_multi_co_schedule_10);
    }
    return g_test_run();
}
