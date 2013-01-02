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

#ifndef EVENT_POLL_H
#define EVENT_POLL_H

#include "qemu/event_notifier.h"

typedef struct EventHandler EventHandler;
typedef void EventCallback(EventHandler *handler);
struct EventHandler {
    EventNotifier *notifier;        /* eventfd */
    EventCallback *callback;        /* callback function */
};

typedef struct {
    int epoll_fd;                   /* epoll(2) file descriptor */
    EventNotifier stop_notifier;    /* stop poll notifier */
    EventHandler stop_handler;      /* stop poll handler */
} EventPoll;

void event_poll_add(EventPoll *poll, EventHandler *handler,
                    EventNotifier *notifier, EventCallback *callback);
void event_poll_init(EventPoll *poll);
void event_poll_cleanup(EventPoll *poll);
void event_poll(EventPoll *poll);
void event_poll_notify(EventPoll *poll);

#endif /* EVENT_POLL_H */
