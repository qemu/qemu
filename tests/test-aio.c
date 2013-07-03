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

#include <glib.h>
#include "block/aio.h"

AioContext *ctx;

/* Wait until there are no more BHs or AIO requests */
static void wait_for_aio(void)
{
    while (aio_poll(ctx, true)) {
        /* Do nothing */
    }
}

/* Simple callbacks for testing.  */

typedef struct {
    QEMUBH *bh;
    int n;
    int max;
} BHTestData;

static void bh_test_cb(void *opaque)
{
    BHTestData *data = opaque;
    if (++data->n < data->max) {
        qemu_bh_schedule(data->bh);
    }
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

typedef struct {
    EventNotifier e;
    int n;
    int active;
    bool auto_set;
} EventNotifierTestData;

static int event_active_cb(EventNotifier *e)
{
    EventNotifierTestData *data = container_of(e, EventNotifierTestData, e);
    return data->active > 0;
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

static void test_notify(void)
{
    g_assert(!aio_poll(ctx, false));
    aio_notify(ctx);
    g_assert(!aio_poll(ctx, true));
    g_assert(!aio_poll(ctx, false));
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

    wait_for_aio();
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

    wait_for_aio();
    g_assert_cmpint(data1.n, ==, data1.max);
    g_assert(data1.bh == NULL);

    g_assert(!aio_poll(ctx, false));
    g_assert(!aio_poll(ctx, true));
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

    wait_for_aio();
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

    wait_for_aio();
    g_assert_cmpint(data.n, ==, 1);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    qemu_bh_delete(data.bh);
}

static void test_set_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 0 };
    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    event_notifier_cleanup(&data.e);
}

static void test_wait_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 1 };
    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 1);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_cleanup(&data.e);
}

static void test_flush_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 10, .auto_set = true };
    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 10);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 9);
    g_assert(aio_poll(ctx, false));

    wait_for_aio();
    g_assert_cmpint(data.n, ==, 10);
    g_assert_cmpint(data.active, ==, 0);
    g_assert(!aio_poll(ctx, false));

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    g_assert(!aio_poll(ctx, false));
    event_notifier_cleanup(&data.e);
}

static void test_wait_event_notifier_noflush(void)
{
    EventNotifierTestData data = { .n = 0 };
    EventNotifierTestData dummy = { .n = 0, .active = 1 };

    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, NULL);

    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 0);

    /* Until there is an active descriptor, aio_poll may or may not call
     * event_ready_cb.  Still, it must not block.  */
    event_notifier_set(&data.e);
    g_assert(!aio_poll(ctx, true));
    data.n = 0;

    /* An active event notifier forces aio_poll to look at EventNotifiers.  */
    event_notifier_init(&dummy.e, false);
    aio_set_event_notifier(ctx, &dummy.e, event_ready_cb, event_active_cb);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_set(&data.e);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);
    g_assert(aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_set(&dummy.e);
    wait_for_aio();
    g_assert_cmpint(data.n, ==, 2);
    g_assert_cmpint(dummy.n, ==, 1);
    g_assert_cmpint(dummy.active, ==, 0);

    aio_set_event_notifier(ctx, &dummy.e, NULL, NULL);
    event_notifier_cleanup(&dummy.e);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    g_assert(!aio_poll(ctx, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_cleanup(&data.e);
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

static void test_source_notify(void)
{
    while (g_main_context_iteration(NULL, false));
    aio_notify(ctx);
    g_assert(g_main_context_iteration(NULL, true));
    g_assert(!g_main_context_iteration(NULL, false));
}

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

    g_assert(!g_main_context_iteration(NULL, false));
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
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    event_notifier_cleanup(&data.e);
}

static void test_source_wait_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 1 };
    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);
    g_assert_cmpint(data.active, ==, 1);

    event_notifier_set(&data.e);
    g_assert(g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.active, ==, 0);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 1);

    event_notifier_cleanup(&data.e);
}

static void test_source_flush_event_notifier(void)
{
    EventNotifierTestData data = { .n = 0, .active = 10, .auto_set = true };
    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, event_active_cb);
    g_assert(g_main_context_iteration(NULL, false));
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

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    while (g_main_context_iteration(NULL, false));
    event_notifier_cleanup(&data.e);
}

static void test_source_wait_event_notifier_noflush(void)
{
    EventNotifierTestData data = { .n = 0 };
    EventNotifierTestData dummy = { .n = 0, .active = 1 };

    event_notifier_init(&data.e, false);
    aio_set_event_notifier(ctx, &data.e, event_ready_cb, NULL);

    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 0);

    /* Until there is an active descriptor, glib may or may not call
     * event_ready_cb.  Still, it must not block.  */
    event_notifier_set(&data.e);
    g_main_context_iteration(NULL, true);
    data.n = 0;

    /* An active event notifier forces aio_poll to look at EventNotifiers.  */
    event_notifier_init(&dummy.e, false);
    aio_set_event_notifier(ctx, &dummy.e, event_ready_cb, event_active_cb);

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

    aio_set_event_notifier(ctx, &dummy.e, NULL, NULL);
    event_notifier_cleanup(&dummy.e);

    aio_set_event_notifier(ctx, &data.e, NULL, NULL);
    while (g_main_context_iteration(NULL, false));
    g_assert_cmpint(data.n, ==, 2);

    event_notifier_cleanup(&data.e);
}

/* End of tests.  */

int main(int argc, char **argv)
{
    GSource *src;

    ctx = aio_context_new();
    src = aio_get_g_source(ctx);
    g_source_attach(src, NULL);
    g_source_unref(src);

    while (g_main_context_iteration(NULL, false));

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/aio/notify",                  test_notify);
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

    g_test_add_func("/aio-gsource/notify",                  test_source_notify);
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
    return g_test_run();
}
