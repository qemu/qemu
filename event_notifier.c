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
#include "qemu-char.h"

#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

void event_notifier_init_fd(EventNotifier *e, int fd)
{
    e->fd = fd;
}

int event_notifier_init(EventNotifier *e, int active)
{
#ifdef CONFIG_EVENTFD
    int fd = eventfd(!!active, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0)
        return -errno;
    e->fd = fd;
    return 0;
#else
    return -ENOSYS;
#endif
}

void event_notifier_cleanup(EventNotifier *e)
{
    close(e->fd);
}

int event_notifier_get_fd(EventNotifier *e)
{
    return e->fd;
}

int event_notifier_set_handler(EventNotifier *e,
                               EventNotifierHandler *handler)
{
    return qemu_set_fd_handler(e->fd, (IOHandler *)handler, NULL, e);
}

int event_notifier_set(EventNotifier *e)
{
    uint64_t value = 1;
    int r = write(e->fd, &value, sizeof(value));
    return r == sizeof(value);
}

int event_notifier_test_and_clear(EventNotifier *e)
{
    uint64_t value;
    int r = read(e->fd, &value, sizeof(value));
    return r == sizeof(value);
}
