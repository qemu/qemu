/*
 * AioContext tests
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Paolo Bonzini    <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/aio.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "qemu/coroutine.h"
#include "qemu/main-loop.h"

static AioContext *ctx;

typedef struct {
    EventNotifier e;
    int n;
    int active;
    bool auto_set;
} EventNotifierTestData;

/* Wait until event notifier becomes inactive */
static void wait_until_inactive(EventNotifierTestData *data)
{
    while (data->active > 0) {
        aio_poll(ctx, true);
    }
}

/* Simple callbacks for testing.  */

typedef struct {
    QEMUBH *bh;
    int n;
    int max;
} BHTestData;

typedef struct {
    QEMUTimer timer;
    QEMUClockType clock_type;
    int n;
    int max;
    int64_t ns;
    AioContext *ctx;
} TimerTestData;

static void bh_test_cb(void *opaque)
{
    BHTestData *data = opaque;
    if (++data->n < data->max) {
        qemu_bh_schedule(data->bh);
    }
}

static void timer_test_cb(void *opaque)
{
    TimerTestData *data = opaque;
    if (++data->n < data->max) {
        timer_mod(&data->timer,
                  qemu_clock_get_ns(data->clock_type) + data->ns);
    }
}

static void dummy_io_handler_read(EventNotifier *e)
{
}

static void bh_delete_cb(void *opaque)
{
    BHTestData *data = opaque;
    if (++data->n < data->max) {
        qemu_bh_schedule(data->bh);
    } else {
        qemu_bh_delete(data->bh);
        data->bh = NULL;
    }
}

static void event_ready_cb(EventNotifier *e)
{
    EventNotifierTestData *data = container_of(e, EventNotifierTestData, e);
    g_assert(event_notifier_test_and_clear(e));
    data->n++;
    if (data->active > 0) {
        data->active--;
    }
    if (data->auto_set && data->active) {
        event_notifier_set(e);
    }
}

/* Tests using aio_*.  */

typedef struct {
    QemuMutex start_lock;
    EventNotifier notifier;
    bool thread_acquired;
} AcquireTestData;

static void *test_acquire_thread(void *opaque)
{
    AcquireTestData *data = opaque;

    /* Wait for other thread to let us start */
    qemu_mutex_lock(&data->start_lock);
    qemu_mutex_unlock(&data->start_lock);

    /* event_notifier_set might be called either before or after
     * the main thread's call to poll().  The test case's outcome
     * should be the same in either case.
     */
    event_notifier_set(&data->notifier);
    aio_context_acquire(ctx);
    aio_context_release(ctx);

    data->thread_acquired = true; /* success, we got here */

    return NULL;
}

static void set_event_notifier(AioContext *ctx, EventNotifier *notifier,
                               EventNotifierHandler *handler)
{
    aio_set_event_notifier(ctx, notifier, false, handler, NULL, NULL);
}

static void dummy_notifier_read(EventNotifier *n)
{
    event_notifier_test_and_clear(n);
}

static void test_acquire(void)
{
    QemuThread thread;
    AcquireTestData data;

    /* Dummy event notifier ensures aio_poll() will block */
    event_notifier_init(&data.notifier, false);
    set_event_notifier(ctx, &data.notifier, dummy_notifier_read);
    g_assert(!aio_poll(ctx, false)); /* consume aio_notify() */

    qemu_mutex_init(&data.start_lock);
    qemu_mutex_lock(&data.start_lock);
    data.thread_acquired = false;

    qemu_thread_create(&thread, "test_acquire_thread",
                       test_acquire_thread,
                       &data, QEMU_THREAD_JOINABLE);

    /* Block in aio_poll(), let other thread kick us and acquire context */
    aio_context_acquire(ctx);
    qemu_mutex_unlock(&data.start_lock); /* let the thread run */
    g_assert(aio_poll(ctx, true));
    g_assert(!data.thread_acquired);
    aio_context_release(ctx);

    qemu_thread_join(&thread);
    set_event_notifier(ctx, &data.notifier, NULL);
    event_notifier_cleanup(&data.notifier);

    g_assert(data.thread_acquired);
}

static void test_bh_schedule(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(aio_poll(ctx, true));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    qemu_bh_delete(data.bh);
}

static void test_bh_schedule10(void)
{
    BHTestData data = { .n = 0, .max = 10 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(aio_poll(ctx, true));
    g_assert_cmpint(data.n, ==, 2);

    while (data.n < 10) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(data.n, ==, 10);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 10);
    qemu_bh_delete(data.bh);
}

static void test_bh_cancel(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    qemu_bh_cancel(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    qemu_bh_delete(data.bh);
}

static void test_bh_delete(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    qemu_bh_delete(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
}

static void test_bh_delete_from_cb(void)
{
    BHTestData data1 = { .n = 0, .max = 1 };

    data1.bh = aio_bh_new(ctx, bh_delete_cb, &data1);

    qemu_bh_schedule(data1.bh);
    g_assert_cmpint(data1.n, ==, 0);

    while (data1.n < data1.max) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(data1.n, ==, data1.max);
    g_assert(data1.bh == NULL);

    g_assert(!aio_poll(ctx, false));
}

static void test_bh_delete_from_cb_many(void)
{
    BHTestData data1 = { .n = 0, .max = 1 };
    BHTestData data2 = { .n = 0, .max = 3 };
    BHTestData data3 = { .n = 0, .max = 2 };
    BHTestData data4 = { .n = 0, .max = 4 };

    data1.bh = aio_bh_new(ctx, bh_delete_cb, &data1);
    data2.bh = aio_bh_new(ctx, bh_delete_cb, &data2);
    data3.bh = aio_bh_new(ctx, bh_delete_cb, &data3);
    data4.bh = aio_bh_new(ctx, bh_delete_cb, &data4);

    qemu_bh_schedule(data1.bh);
    qemu_bh_schedule(data2.bh);
    qemu_bh_schedule(data3.bh);
    qemu_bh_schedule(data4.bh);
    g_assert_cmpint(data1.n, ==, 0);
    g_assert_cmpint(data2.n, ==, 0);
    g_assert_cmpint(data3.n, ==, 0);
    g_assert_cmpint(data4.n, ==, 0);

    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data1.n, ==, 1);
    g_assert_cmpint(data2.n, ==, 1);
    g_assert_cmpint(data3.n, ==, 1);
    g_assert_cmpint(data4.n, ==, 1);
    g_assert(data1.bh == NULL);

    while (data1.n < data1.max ||
           data2.n < data2.max ||
           data3.n < data3.max ||
           data4.n < data4.max) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(data1.n, ==, data1.max);
    g_assert_cmpint(data2.n, ==, data2.max);
    g_assert_cmpint(data3.n, ==, data3.max);
    g_assert_cmpint(data4.n, ==, data4.max);
    g_assert(data1.bh == NULL);
    g_assert(data2.bh == NULL);
    g_assert(data3.bh == NULL);
    g_assert(data4.bh == NULL);
}

static void test_bh_flush(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(aio_poll(ctx, true));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    qemu_bh_delete(data.bh);
}

static void test_set_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 0 };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);

    set_event_notifier(ctx, &data.e, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    event_notifier_cleanup(&data.e);
}

static void test_wait_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 1 };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    while (aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 1);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    set_event_notifier(ctx, &data.e, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_cleanup(&data.e);
}

static void test_flush_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 10, .auto_set = true };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    while (aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 10);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 9);
    g_assert(aio_poll(ctx, false));

    wait_until_inactive(&data);
    g_assert_cmpint(data.n, ==, 10);
    g_assert_cmpint(data.active, ==, 0);
    g_assert(!aio_poll(ctx, false));

    set_event_notifier(ctx, &data.e, NULL);
    g_assert(!aio_poll(ctx, false));
    event_notifier_cleanup(&data.e);
}

static void test_aio_external_client(void)
{
    int i, j;

    for (i = 1; i < 3; i++) {
        EventNotifierTestData data = { .n = 0, .active = 10, .auto_set = true };
        event_notifier_init(&data.e, false);
        aio_set_event_notifier(ctx, &data.e, true, event_ready_cb, NULL, NULL);
        event_notifier_set(&data.e);
        for (j = 0; j < i; j++) {
            aio_disable_external(ctx);
        }
        for (j = 0; j < i; j++) {
            assert(!aio_poll(ctx, false));
            assert(event_notifier_test_and_clear(&data.e));
            event_notifier_set(&data.e);
            aio_enable_external(ctx);
        }
        assert(aio_poll(ctx, false));
        set_event_notifier(ctx, &data.e, NULL);
        event_notifier_cleanup(&data.e);
    }
}

static void test_wait_event_notifier_noflush(void)
{
    EventNotifierTestData data = { .n = 0 };
    EventNotifierTestData dummy = { .n = 0, .active = 1 };

    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);

    /* Until there is an active descriptor, aio_poll may or may not call
     * event_ready_cb.  Still, it must not block.  */
    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, true));
    data.n = 0;

    /* An active event notifier forces aio_poll to look at EventNotifiers.  */
    event_notifier_init(&dummy.e, false);
    set_event_notifier(ctx, &dummy.e, event_ready_cb);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_set(&dummy.e);
    wait_until_inactive(&dummy);
    g_assert_cmpint(data.n, ==, 2);
    g_assert_cmpint(dummy.n, ==, 1);
    g_assert_cmpint(dummy.active, ==, 0);

    set_event_notifier(ctx, &dummy.e, NULL);
    event_notifier_cleanup(&dummy.e);

    set_event_notifier(ctx, &data.e, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_cleanup(&data.e);
}

static void test_timer_schedule(void)
{
    TimerTestData data = { .n = 0, .ctx = ctx, .ns = SCALE_MS * 750LL,
                           .max = 2,
                           .clock_type = QEMU_CLOCK_REALTIME };
    EventNotifier e;

    /* aio_poll will not block to wait for timers to complete unless it has
     * an fd to wait on. Fixing this breaks other tests. So create a dummy one.
     */
    event_notifier_init(&e, false);
    set_event_notifier(ctx, &e, dummy_io_handler_read);
    aio_poll(ctx, false);

    aio_timer_init(ctx, &data.timer, data.clock_type,
                   SCALE_NS, timer_test_cb, &data);
    timer_mod(&data.timer,
              qemu_clock_get_ns(data.clock_type) +
              data.ns);

    g_assert_cmpint(data.n, ==, 0);

    /* timer_mod may well cause an event notifer to have gone off,
     * so clear that
     */
    do {} while (aio_poll(ctx, false));

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);

    g_usleep(1 * G_USEC_PER_SEC);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    /* timer_mod called by our callback */
    do {} while (aio_poll(ctx, false));

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(aio_poll(ctx, true));
    g_assert_cmpint(data.n, ==, 2);

    /* As max is now 2, an event notifier should not have gone off */

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);

    set_event_notifier(ctx, &e, NULL);
    event_notifier_cleanup(&e);

    timer_del(&data.timer);
}

/* Now the same tests, using the context as a GSource.  They are
 * very similar to the ones above, with g_main_context_iteration
 * replacing aio_poll.  However:
 * - sometimes both the AioContext and the glib main loop wake
 *   themselves up.  Hence, some "g_assert(!aio_poll(ctx, false));"
 *   are replaced by "while (g_main_context_iteration(NULL, false));".
 * - there is no exact replacement for a blocking wait.
 *   "while (g_main_context_iteration(NULL, true)" seems to work,
 *   but it is not documented _why_ it works.  For these tests a
 *   non-blocking loop like "while (g_main_context_iteration(NULL, false)"
 *   works well, and that's what I am using.
 */

static void test_source_flush(void)
{
    g_assert(!g_main_context_iteration(NULL, false));
    aio_notify(ctx);
    while (g_main_context_iteration(NULL, false));
    g_assert(!g_main_context_iteration(NULL, false));
}

static void test_source_bh_schedule(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(g_main_context_iteration(NULL, true));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(!g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    qemu_bh_delete(data.bh);
}

static void test_source_bh_schedule10(void)
{
    BHTestData data = { .n = 0, .max = 10 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(g_main_context_iteration(NULL, true));
    g_assert_cmpint(data.n, ==, 2);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 10);

    g_assert(!g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 10);
    qemu_bh_delete(data.bh);
}

static void test_source_bh_cancel(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    qemu_bh_cancel(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    qemu_bh_delete(data.bh);
}

static void test_source_bh_delete(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    qemu_bh_delete(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
}

static void test_source_bh_delete_from_cb(void)
{
    BHTestData data1 = { .n = 0, .max = 1 };

    data1.bh = aio_bh_new(ctx, bh_delete_cb, &data1);

    qemu_bh_schedule(data1.bh);
    g_assert_cmpint(data1.n, ==, 0);

    g_main_context_iteration(NULL, true);
    g_assert_cmpint(data1.n, ==, data1.max);
    g_assert(data1.bh == NULL);

    assert(g_main_context_iteration(NULL, false));
    assert(!g_main_context_iteration(NULL, false));
}

static void test_source_bh_delete_from_cb_many(void)
{
    BHTestData data1 = { .n = 0, .max = 1 };
    BHTestData data2 = { .n = 0, .max = 3 };
    BHTestData data3 = { .n = 0, .max = 2 };
    BHTestData data4 = { .n = 0, .max = 4 };

    data1.bh = aio_bh_new(ctx, bh_delete_cb, &data1);
    data2.bh = aio_bh_new(ctx, bh_delete_cb, &data2);
    data3.bh = aio_bh_new(ctx, bh_delete_cb, &data3);
    data4.bh = aio_bh_new(ctx, bh_delete_cb, &data4);

    qemu_bh_schedule(data1.bh);
    qemu_bh_schedule(data2.bh);
    qemu_bh_schedule(data3.bh);
    qemu_bh_schedule(data4.bh);
    g_assert_cmpint(data1.n, ==, 0);
    g_assert_cmpint(data2.n, ==, 0);
    g_assert_cmpint(data3.n, ==, 0);
    g_assert_cmpint(data4.n, ==, 0);

    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data1.n, ==, 1);
    g_assert_cmpint(data2.n, ==, 1);
    g_assert_cmpint(data3.n, ==, 1);
    g_assert_cmpint(data4.n, ==, 1);
    g_assert(data1.bh == NULL);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data1.n, ==, data1.max);
    g_assert_cmpint(data2.n, ==, data2.max);
    g_assert_cmpint(data3.n, ==, data3.max);
    g_assert_cmpint(data4.n, ==, data4.max);
    g_assert(data1.bh == NULL);
    g_assert(data2.bh == NULL);
    g_assert(data3.bh == NULL);
    g_assert(data4.bh == NULL);
}

static void test_source_bh_flush(void)
{
    BHTestData data = { .n = 0 };
    data.bh = aio_bh_new(ctx, bh_test_cb, &data);

    qemu_bh_schedule(data.bh);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(g_main_context_iteration(NULL, true));
    g_assert_cmpint(data.n, ==, 1);

    g_assert(!g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    qemu_bh_delete(data.bh);
}

static void test_source_set_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 0 };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);

    set_event_notifier(ctx, &data.e, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    event_notifier_cleanup(&data.e);
}

static void test_source_wait_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 1 };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 1);

    event_notifier_set(&data.e);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    set_event_notifier(ctx, &data.e, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_cleanup(&data.e);
}

static void test_source_flush_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 10, .auto_set = true };
    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 10);

    event_notifier_set(&data.e);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 9);
    g_assert(g_main_context_iteration(NULL, false));

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 10);
    g_assert_cmpint(data.active, ==, 0);
    g_assert(!g_main_context_iteration(NULL, false));

    set_event_notifier(ctx, &data.e, NULL);
    while (g_main_context_iteration(NULL, false));
    event_notifier_cleanup(&data.e);
}

static void test_source_wait_event_notifier_noflush(void)
{
    EventNotifierTestData data = { .n = 0 };
    EventNotifierTestData dummy = { .n = 0, .active = 1 };

    event_notifier_init(&data.e, false);
    set_event_notifier(ctx, &data.e, event_ready_cb);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);

    /* Until there is an active descriptor, glib may or may not call
     * event_ready_cb.  Still, it must not block.  */
    event_notifier_set(&data.e);
    g_main_context_iteration(NULL, true);
    data.n = 0;

    /* An active event notifier forces aio_poll to look at EventNotifiers.  */
    event_notifier_init(&dummy.e, false);
    set_event_notifier(ctx, &dummy.e, event_ready_cb);

    event_notifier_set(&data.e);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert(!g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_set(&data.e);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 2);
    g_assert(!g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_set(&dummy.e);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 2);
    g_assert_cmpint(dummy.n, ==, 1);
    g_assert_cmpint(dummy.active, ==, 0);

    set_event_notifier(ctx, &dummy.e, NULL);
    event_notifier_cleanup(&dummy.e);

    set_event_notifier(ctx, &data.e, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_cleanup(&data.e);
}

static void test_source_timer_schedule(void)
{
    TimerTestData data = { .n = 0, .ctx = ctx, .ns = SCALE_MS * 750LL,
                           .max = 2,
                           .clock_type = QEMU_CLOCK_REALTIME };
    EventNotifier e;
    int64_t expiry;

    /* aio_poll will not block to wait for timers to complete unless it has
     * an fd to wait on. Fixing this breaks other tests. So create a dummy one.
     */
    event_notifier_init(&e, false);
    set_event_notifier(ctx, &e, dummy_io_handler_read);
    do {} while (g_main_context_iteration(NULL, false));

    aio_timer_init(ctx, &data.timer, data.clock_type,
                   SCALE_NS, timer_test_cb, &data);
    expiry = qemu_clock_get_ns(data.clock_type) +
        data.ns;
    timer_mod(&data.timer, expiry);

    g_assert_cmpint(data.n, ==, 0);

    g_usleep(1 * G_USEC_PER_SEC);
    g_assert_cmpint(data.n, ==, 0);

    g_assert(g_main_context_iteration(NULL, true));
    g_assert_cmpint(data.n, ==, 1);
    expiry += data.ns;

    while (data.n < 2) {
        g_main_context_iteration(NULL, true);
    }

    g_assert_cmpint(data.n, ==, 2);
    g_assert(qemu_clock_get_ns(data.clock_type) > expiry);

    set_event_notifier(ctx, &e, NULL);
    event_notifier_cleanup(&e);

    timer_del(&data.timer);
}

/*
 * Check that aio_co_enter() can chain many times
 *
 * Two coroutines should be able to invoke each other via aio_co_enter() many
 * times without hitting a limit like stack exhaustion.  In other words, the
 * calls should be chained instead of nested.
 */

typedef struct {
    Coroutine *other;
    unsigned i;
    unsigned max;
} ChainData;

static void coroutine_fn chain(void *opaque)
{
    ChainData *data = opaque;

    for (data->i = 0; data->i < data->max; data->i++) {
        /* Queue up the other coroutine... */
        aio_co_enter(ctx, data->other);

        /* ...and give control to it */
        qemu_coroutine_yield();
    }
}

static void test_queue_chaining(void)
{
    /* This number of iterations hit stack exhaustion in the past: */
    ChainData data_a = { .max = 25000 };
    ChainData data_b = { .max = 25000 };

    data_b.other = qemu_coroutine_create(chain, &data_a);
    data_a.other = qemu_coroutine_create(chain, &data_b);

    qemu_coroutine_enter(data_b.other);

    g_assert_cmpint(data_a.i, ==, data_a.max);
    g_assert_cmpint(data_b.i, ==, data_b.max - 1);

    /* Allow the second coroutine to terminate */
    qemu_coroutine_enter(data_a.other);

    g_assert_cmpint(data_b.i, ==, data_b.max);
}

static void co_check_current_thread(void *opaque)
{
    QemuThread *main_thread = opaque;
    assert(qemu_thread_is_self(main_thread));
}

static void *test_aio_co_enter(void *co)
{
    /*
     * qemu_get_current_aio_context() should not to be the main thread
     * AioContext, because this is a worker thread that has not taken
     * the BQL.  So aio_co_enter will schedule the coroutine in the
     * main thread AioContext.
     */
    aio_co_enter(qemu_get_aio_context(), co);
    return NULL;
}

static void test_worker_thread_co_enter(void)
{
    QemuThread this_thread, worker_thread;
    Coroutine *co;

    qemu_thread_get_self(&this_thread);
    co = qemu_coroutine_create(co_check_current_thread, &this_thread);

    qemu_thread_create(&worker_thread, "test_acquire_thread",
                       test_aio_co_enter,
                       co, QEMU_THREAD_JOINABLE);

    /* Test aio_co_enter from a worker thread.  */
    qemu_thread_join(&worker_thread);
    g_assert(aio_poll(ctx, true));
    g_assert(!aio_poll(ctx, false));
}

/* End of tests.  */

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_fatal);
    ctx = qemu_get_aio_context();

    while (g_main_context_iteration(NULL, false));

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/aio/acquire",                 test_acquire);
    g_test_add_func("/aio/bh/schedule",             test_bh_schedule);
    g_test_add_func("/aio/bh/schedule10",           test_bh_schedule10);
    g_test_add_func("/aio/bh/cancel",               test_bh_cancel);
    g_test_add_func("/aio/bh/delete",               test_bh_delete);
    g_test_add_func("/aio/bh/callback-delete/one",  test_bh_delete_from_cb);
    g_test_add_func("/aio/bh/callback-delete/many", test_bh_delete_from_cb_many);
    g_test_add_func("/aio/bh/flush",                test_bh_flush);
    g_test_add_func("/aio/event/add-remove",        test_set_event_notifier);
    g_test_add_func("/aio/event/wait",              test_wait_event_notifier);
    g_test_add_func("/aio/event/wait/no-flush-cb",  test_wait_event_notifier_noflush);
    g_test_add_func("/aio/event/flush",             test_flush_event_notifier);
    g_test_add_func("/aio/external-client",         test_aio_external_client);
    g_test_add_func("/aio/timer/schedule",          test_timer_schedule);

    g_test_add_func("/aio/coroutine/queue-chaining", test_queue_chaining);
    g_test_add_func("/aio/coroutine/worker-thread-co-enter", test_worker_thread_co_enter);

    g_test_add_func("/aio-gsource/flush",                   test_source_flush);
    g_test_add_func("/aio-gsource/bh/schedule",             test_source_bh_schedule);
    g_test_add_func("/aio-gsource/bh/schedule10",           test_source_bh_schedule10);
    g_test_add_func("/aio-gsource/bh/cancel",               test_source_bh_cancel);
    g_test_add_func("/aio-gsource/bh/delete",               test_source_bh_delete);
    g_test_add_func("/aio-gsource/bh/callback-delete/one",  test_source_bh_delete_from_cb);
    g_test_add_func("/aio-gsource/bh/callback-delete/many", test_source_bh_delete_from_cb_many);
    g_test_add_func("/aio-gsource/bh/flush",                test_source_bh_flush);
    g_test_add_func("/aio-gsource/event/add-remove",        test_source_set_event_notifier);
    g_test_add_func("/aio-gsource/event/wait",              test_source_wait_event_notifier);
    g_test_add_func("/aio-gsource/event/wait/no-flush-cb",  test_source_wait_event_notifier_noflush);
    g_test_add_func("/aio-gsource/event/flush",             test_source_flush_event_notifier);
    g_test_add_func("/aio-gsource/timer/schedule",          test_source_timer_schedule);
    return g_test_run();
}
