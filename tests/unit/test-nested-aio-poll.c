/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Test that poll handlers are not re-entrant in nested aio_poll()
 *
 * Copyright Red Hat
 *
 * Poll handlers are usually level-triggered. That means they continue firing
 * until the condition is reset (e.g. a virtqueue becomes empty). If a poll
 * handler calls nested aio_poll() before the condition is reset, then infinite
 * recursion occurs.
 *
 * aio_poll() is supposed to prevent this by disabling poll handlers in nested
 * aio_poll() calls. This test case checks that this is indeed what happens.
 */
#include "qemu/osdep.h"
#include "block/aio.h"
#include "qapi/error.h"
#include "util/aio-posix.h"

typedef struct {
    AioContext *ctx;

    /* This is the EventNotifier that drives the test */
    EventNotifier poll_notifier;

    /* This EventNotifier is only used to wake aio_poll() */
    EventNotifier dummy_notifier;

    bool nested;
} TestData;

static void io_read(EventNotifier *notifier)
{
    event_notifier_test_and_clear(notifier);
}

static bool io_poll_true(void *opaque)
{
    return true;
}

static bool io_poll_false(void *opaque)
{
    return false;
}

static void io_poll_ready(EventNotifier *notifier)
{
    TestData *td = container_of(notifier, TestData, poll_notifier);

    g_assert(!td->nested);
    td->nested = true;

    /* Wake the following nested aio_poll() call */
    event_notifier_set(&td->dummy_notifier);

    /* This nested event loop must not call io_poll()/io_poll_ready() */
    g_assert(aio_poll(td->ctx, true));

    td->nested = false;
}

/* dummy_notifier never triggers */
static void io_poll_never_ready(EventNotifier *notifier)
{
    g_assert_not_reached();
}

static void test(void)
{
    TestData td = {
        .ctx = aio_context_new(&error_abort),
    };

    if (td.ctx->fdmon_ops != &fdmon_poll_ops) {
        /* This test is tied to fdmon-poll.c */
        g_test_skip("fdmon_poll_ops not in use");
        return;
    }

    qemu_set_current_aio_context(td.ctx);

    /* Enable polling */
    aio_context_set_poll_params(td.ctx, 1000000, 2, 2, &error_abort);

    /* Make the event notifier active (set) right away */
    event_notifier_init(&td.poll_notifier, 1);
    aio_set_event_notifier(td.ctx, &td.poll_notifier,
                           io_read, io_poll_true, io_poll_ready);

    /* This event notifier will be used later */
    event_notifier_init(&td.dummy_notifier, 0);
    aio_set_event_notifier(td.ctx, &td.dummy_notifier,
                           io_read, io_poll_false, io_poll_never_ready);

    /* Consume aio_notify() */
    g_assert(!aio_poll(td.ctx, false));

    /*
     * Run the io_read() handler. This has the side-effect of activating
     * polling in future aio_poll() calls.
     */
    g_assert(aio_poll(td.ctx, true));

    /* The second time around the io_poll()/io_poll_ready() handler runs */
    g_assert(aio_poll(td.ctx, true));

    /* Run io_poll()/io_poll_ready() one more time to show it keeps working */
    g_assert(aio_poll(td.ctx, true));

    aio_set_event_notifier(td.ctx, &td.dummy_notifier, NULL, NULL, NULL);
    aio_set_event_notifier(td.ctx, &td.poll_notifier, NULL, NULL, NULL);
    event_notifier_cleanup(&td.dummy_notifier);
    event_notifier_cleanup(&td.poll_notifier);
    aio_context_unref(td.ctx);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nested-aio-poll", test);
    return g_test_run();
}
