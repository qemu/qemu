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

#include "qemu-common.h"
#include "event_notifier.h"
#include "main-loop.h"

int event_notifier_init(EventNotifier *e, int active)
{
    e->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(e->event);
    return 0;
}

void event_notifier_cleanup(EventNotifier *e)
{
    CloseHandle(e->event);
}

HANDLE event_notifier_get_handle(EventNotifier *e)
{
    return e->event;
}

int event_notifier_set_handler(EventNotifier *e,
                               EventNotifierHandler *handler)
{
    if (handler) {
        return qemu_add_wait_object(e->event, (IOHandler *)handler, e);
    } else {
        qemu_del_wait_object(e->event, (IOHandler *)handler, e);
        return 0;
    }
}

int event_notifier_set(EventNotifier *e)
{
    SetEvent(e->event);
    return 0;
}

int event_notifier_test_and_clear(EventNotifier *e)
{
    int ret = WaitForSingleObject(e->event, 0);
    if (ret == WAIT_OBJECT_0) {
        ResetEvent(e->event);
        return true;
    }
    return false;
}
