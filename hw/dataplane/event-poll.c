/*
 * Event loop with file descriptor polling
 *
 * Copyright 2012 IBM, Corp.
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <sys/epoll.h>
#include "hw/dataplane/event-poll.h"

/* Add an event notifier and its callback for polling */
void event_poll_add(EventPoll *poll, EventHandler *handler,
                    EventNotifier *notifier, EventCallback *callback)
{
    struct epoll_event event = {
        .events = EPOLLIN,
        .data.ptr = handler,
    };
    handler->notifier = notifier;
    handler->callback = callback;
    if (epoll_ctl(poll->epoll_fd, EPOLL_CTL_ADD,
                  event_notifier_get_fd(notifier), &event) != 0) {
        fprintf(stderr, "failed to add event handler to epoll: %m\n");
        exit(1);
    }
}

/* Event callback for stopping event_poll() */
static void handle_stop(EventHandler *handler)
{
    /* Do nothing */
}

void event_poll_init(EventPoll *poll)
{
    /* Create epoll file descriptor */
    poll->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (poll->epoll_fd < 0) {
        fprintf(stderr, "epoll_create1 failed: %m\n");
        exit(1);
    }

    /* Set up stop notifier */
    if (event_notifier_init(&poll->stop_notifier, 0) < 0) {
        fprintf(stderr, "failed to init stop notifier\n");
        exit(1);
    }
    event_poll_add(poll, &poll->stop_handler,
                   &poll->stop_notifier, handle_stop);
}

void event_poll_cleanup(EventPoll *poll)
{
    event_notifier_cleanup(&poll->stop_notifier);
    close(poll->epoll_fd);
    poll->epoll_fd = -1;
}

/* Block until the next event and invoke its callback */
void event_poll(EventPoll *poll)
{
    EventHandler *handler;
    struct epoll_event event;
    int nevents;

    /* Wait for the next event.  Only do one event per call to keep the
     * function simple, this could be changed later. */
    do {
        nevents = epoll_wait(poll->epoll_fd, &event, 1, -1);
    } while (nevents < 0 && errno == EINTR);
    if (unlikely(nevents != 1)) {
        fprintf(stderr, "epoll_wait failed: %m\n");
        exit(1); /* should never happen */
    }

    /* Find out which event handler has become active */
    handler = event.data.ptr;

    /* Clear the eventfd */
    event_notifier_test_and_clear(handler->notifier);

    /* Handle the event */
    handler->callback(handler);
}

/* Stop event_poll()
 *
 * This function can be used from another thread.
 */
void event_poll_notify(EventPoll *poll)
{
    event_notifier_set(&poll->stop_notifier);
}
