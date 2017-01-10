/*
 * event notifier support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_EVENT_NOTIFIER_H
#define QEMU_EVENT_NOTIFIER_H

#include "qemu-common.h"

#ifdef _WIN32
#include <windows.h>
#endif

struct EventNotifier {
#ifdef _WIN32
    HANDLE event;
#else
    int rfd;
    int wfd;
#endif
};

typedef void EventNotifierHandler(EventNotifier *);

int event_notifier_init(EventNotifier *, int active);
void event_notifier_cleanup(EventNotifier *);
int event_notifier_set(EventNotifier *);
int event_notifier_test_and_clear(EventNotifier *);

#ifdef CONFIG_POSIX
void event_notifier_init_fd(EventNotifier *, int fd);
int event_notifier_get_fd(const EventNotifier *);
#else
HANDLE event_notifier_get_handle(EventNotifier *);
#endif

#endif
