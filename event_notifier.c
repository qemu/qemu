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

#include "event_notifier.h"
#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

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

int event_notifier_test_and_clear(EventNotifier *e)
{
    uint64_t value;
    int r = read(e->fd, &value, sizeof(value));
    return r == sizeof(value);
}

int event_notifier_test(EventNotifier *e)
{
    uint64_t value;
    int r = read(e->fd, &value, sizeof(value));
    if (r == sizeof(value)) {
        /* restore previous value. */
        int s = write(e->fd, &value, sizeof(value));
        /* never blocks because we use EFD_SEMAPHORE.
         * If we didn't we'd get EAGAIN on overflow
         * and we'd have to write code to ignore it. */
        assert(s == sizeof(value));
    }
    return r == sizeof(value);
}
