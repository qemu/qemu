/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * fdmon-epoll tests
 *
 * Copyright (c) 2020 Red Hat, Inc.
 */

#include "qemu/osdep.h"
#include "block/aio.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"

static AioContext *ctx;

static void dummy_fd_handler(EventNotifier *notifier)
{
    event_notifier_test_and_clear(notifier);
}

static void add_event_notifiers(EventNotifier *notifiers, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        event_notifier_init(&notifiers[i], false);
        aio_set_event_notifier(ctx, &notifiers[i], false,
                               dummy_fd_handler, NULL);
    }
}

static void remove_event_notifiers(EventNotifier *notifiers, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        aio_set_event_notifier(ctx, &notifiers[i], false, NULL, NULL);
        event_notifier_cleanup(&notifiers[i]);
    }
}

/* Check that fd handlers work when external clients are disabled */
static void test_external_disabled(void)
{
    EventNotifier notifiers[100];

    /* fdmon-epoll is only enabled when many fd handlers are registered */
    add_event_notifiers(notifiers, G_N_ELEMENTS(notifiers));

    event_notifier_set(&notifiers[0]);
    assert(aio_poll(ctx, true));

    aio_disable_external(ctx);
    event_notifier_set(&notifiers[0]);
    assert(aio_poll(ctx, true));
    aio_enable_external(ctx);

    remove_event_notifiers(notifiers, G_N_ELEMENTS(notifiers));
}

int main(int argc, char **argv)
{
    /*
     * This code relies on the fact that fdmon-io_uring disables itself when
     * the glib main loop is in use. The main loop uses fdmon-poll and upgrades
     * to fdmon-epoll when the number of fds exceeds a threshold.
     */
    qemu_init_main_loop(&error_fatal);
    ctx = qemu_get_aio_context();

    while (g_main_context_iteration(NULL, false)) {
        /* Do nothing */
    }

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/fdmon-epoll/external-disabled", test_external_disabled);
    return g_test_run();
}
