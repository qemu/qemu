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

/* CoMutex thread-safety.  */

static uint32_t atomic_counter;
static uint32_t running;
static uint32_t counter;
static CoMutex comutex;

static void coroutine_fn test_multi_co_mutex_entry(void *opaque)
{
    while (!atomic_mb_read(&now_stopping)) {
        qemu_co_mutex_lock(&comutex);
        counter++;
        qemu_co_mutex_unlock(&comutex);

        /* Increase atomic_counter *after* releasing the mutex.  Otherwise
         * there is a chance (it happens about 1 in 3 runs) that the iothread
         * exits before the coroutine is woken up, causing a spurious
         * assertion failure.
         */
        atomic_inc(&atomic_counter);
    }
    atomic_dec(&running);
}

static void test_multi_co_mutex(int threads, int seconds)
{
    int i;

    qemu_co_mutex_init(&comutex);
    counter = 0;
    atomic_counter = 0;
    now_stopping = false;

    create_aio_contexts();
    assert(threads <= NUM_CONTEXTS);
    running = threads;
    for (i = 0; i < threads; i++) {
        Coroutine *co1 = qemu_coroutine_create(test_multi_co_mutex_entry, NULL);
        aio_co_schedule(ctx[i], co1);
    }

    g_usleep(seconds * 1000000);

    atomic_mb_set(&now_stopping, true);
    while (running > 0) {
        g_usleep(100000);
    }

    join_aio_contexts();
    g_test_message("%d iterations/second\n", counter / seconds);
    g_assert_cmpint(counter, ==, atomic_counter);
}

/* Testing with NUM_CONTEXTS threads focuses on the queue.  The mutex however
 * is too contended (and the threads spend too much time in aio_poll)
 * to actually stress the handoff protocol.
 */
static void test_multi_co_mutex_1(void)
{
    test_multi_co_mutex(NUM_CONTEXTS, 1);
}

static void test_multi_co_mutex_10(void)
{
    test_multi_co_mutex(NUM_CONTEXTS, 10);
}

/* Testing with fewer threads stresses the handoff protocol too.  Still, the
 * case where the locker _can_ pick up a handoff is very rare, happening
 * about 10 times in 1 million, so increase the runtime a bit compared to
 * other "quick" testcases that only run for 1 second.
 */
static void test_multi_co_mutex_2_3(void)
{
    test_multi_co_mutex(2, 3);
}

static void test_multi_co_mutex_2_30(void)
{
    test_multi_co_mutex(2, 30);
}

/* Same test with fair mutexes, for performance comparison.  */

#ifdef CONFIG_LINUX
#include "qemu/futex.h"

/* The nodes for the mutex reside in this structure (on which we try to avoid
 * false sharing).  The head of the mutex is in the "mutex_head" variable.
 */
static struct {
    int next, locked;
    int padding[14];
} nodes[NUM_CONTEXTS] __attribute__((__aligned__(64)));

static int mutex_head = -1;

static void mcs_mutex_lock(void)
{
    int prev;

    nodes[id].next = -1;
    nodes[id].locked = 1;
    prev = atomic_xchg(&mutex_head, id);
    if (prev != -1) {
        atomic_set(&nodes[prev].next, id);
        qemu_futex_wait(&nodes[id].locked, 1);
    }
}

static void mcs_mutex_unlock(void)
{
    int next;
    if (atomic_read(&nodes[id].next) == -1) {
        if (atomic_read(&mutex_head) == id &&
            atomic_cmpxchg(&mutex_head, id, -1) == id) {
            /* Last item in the list, exit.  */
            return;
        }
        while (atomic_read(&nodes[id].next) == -1) {
            /* mcs_mutex_lock did the xchg, but has not updated
             * nodes[prev].next yet.
             */
        }
    }

    /* Wake up the next in line.  */
    next = atomic_read(&nodes[id].next);
    nodes[next].locked = 0;
    qemu_futex_wake(&nodes[next].locked, 1);
}

static void test_multi_fair_mutex_entry(void *opaque)
{
    while (!atomic_mb_read(&now_stopping)) {
        mcs_mutex_lock();
        counter++;
        mcs_mutex_unlock();
        atomic_inc(&atomic_counter);
    }
    atomic_dec(&running);
}

static void test_multi_fair_mutex(int threads, int seconds)
{
    int i;

    assert(mutex_head == -1);
    counter = 0;
    atomic_counter = 0;
    now_stopping = false;

    create_aio_contexts();
    assert(threads <= NUM_CONTEXTS);
    running = threads;
    for (i = 0; i < threads; i++) {
        Coroutine *co1 = qemu_coroutine_create(test_multi_fair_mutex_entry, NULL);
        aio_co_schedule(ctx[i], co1);
    }

    g_usleep(seconds * 1000000);

    atomic_mb_set(&now_stopping, true);
    while (running > 0) {
        g_usleep(100000);
    }

    join_aio_contexts();
    g_test_message("%d iterations/second\n", counter / seconds);
    g_assert_cmpint(counter, ==, atomic_counter);
}

static void test_multi_fair_mutex_1(void)
{
    test_multi_fair_mutex(NUM_CONTEXTS, 1);
}

static void test_multi_fair_mutex_10(void)
{
    test_multi_fair_mutex(NUM_CONTEXTS, 10);
}
#endif

/* Same test with pthread mutexes, for performance comparison and
 * portability.  */

static QemuMutex mutex;

static void test_multi_mutex_entry(void *opaque)
{
    while (!atomic_mb_read(&now_stopping)) {
        qemu_mutex_lock(&mutex);
        counter++;
        qemu_mutex_unlock(&mutex);
        atomic_inc(&atomic_counter);
    }
    atomic_dec(&running);
}

static void test_multi_mutex(int threads, int seconds)
{
    int i;

    qemu_mutex_init(&mutex);
    counter = 0;
    atomic_counter = 0;
    now_stopping = false;

    create_aio_contexts();
    assert(threads <= NUM_CONTEXTS);
    running = threads;
    for (i = 0; i < threads; i++) {
        Coroutine *co1 = qemu_coroutine_create(test_multi_mutex_entry, NULL);
        aio_co_schedule(ctx[i], co1);
    }

    g_usleep(seconds * 1000000);

    atomic_mb_set(&now_stopping, true);
    while (running > 0) {
        g_usleep(100000);
    }

    join_aio_contexts();
    g_test_message("%d iterations/second\n", counter / seconds);
    g_assert_cmpint(counter, ==, atomic_counter);
}

static void test_multi_mutex_1(void)
{
    test_multi_mutex(NUM_CONTEXTS, 1);
}

static void test_multi_mutex_10(void)
{
    test_multi_mutex(NUM_CONTEXTS, 10);
}

/* End of tests.  */

int main(int argc, char **argv)
{
    init_clocks(NULL);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/aio/multi/lifecycle", test_lifecycle);
    if (g_test_quick()) {
        g_test_add_func("/aio/multi/schedule", test_multi_co_schedule_1);
        g_test_add_func("/aio/multi/mutex/contended", test_multi_co_mutex_1);
        g_test_add_func("/aio/multi/mutex/handoff", test_multi_co_mutex_2_3);
#ifdef CONFIG_LINUX
        g_test_add_func("/aio/multi/mutex/mcs", test_multi_fair_mutex_1);
#endif
        g_test_add_func("/aio/multi/mutex/pthread", test_multi_mutex_1);
    } else {
        g_test_add_func("/aio/multi/schedule", test_multi_co_schedule_10);
        g_test_add_func("/aio/multi/mutex/contended", test_multi_co_mutex_10);
        g_test_add_func("/aio/multi/mutex/handoff", test_multi_co_mutex_2_30);
#ifdef CONFIG_LINUX
        g_test_add_func("/aio/multi/mutex/mcs", test_multi_fair_mutex_10);
#endif
        g_test_add_func("/aio/multi/mutex/pthread", test_multi_mutex_10);
    }
    return g_test_run();
}
