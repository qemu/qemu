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
#include "qemu/event_notifier.h"
#include "sysemu/char.h"
#include "qemu/main-loop.h"

#ifdef CONFIG_EVENTFD
#include <sys/eventfd.h>
#endif

void event_notifier_init_fd(EventNotifier *e, int fd)
{
    e->rfd = fd;
    e->wfd = fd;
}

int event_notifier_init(EventNotifier *e, int active)
{
    int fds[2];
    int ret;

#ifdef CONFIG_EVENTFD
    ret = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#else
    ret = -1;
    errno = ENOSYS;
#endif
    if (ret >= 0) {
        e->rfd = e->wfd = ret;
    } else {
        if (errno != ENOSYS) {
            return -errno;
        }
        if (qemu_pipe(fds) < 0) {
            return -errno;
        }
        ret = fcntl_setfl(fds[0], O_NONBLOCK);
        if (ret < 0) {
            ret = -errno;
            goto fail;
        }
        ret = fcntl_setfl(fds[1], O_NONBLOCK);
        if (ret < 0) {
            ret = -errno;
            goto fail;
        }
        e->rfd = fds[0];
        e->wfd = fds[1];
    }
    if (active) {
        event_notifier_set(e);
    }
    return 0;

fail:
    close(fds[0]);
    close(fds[1]);
    return ret;
}

void event_notifier_cleanup(EventNotifier *e)
{
    if (e->rfd != e->wfd) {
        close(e->rfd);
    }
    close(e->wfd);
}

int event_notifier_get_fd(EventNotifier *e)
{
    return e->rfd;
}

int event_notifier_set_handler(EventNotifier *e,
                               EventNotifierHandler *handler)
{
    return qemu_set_fd_handler(e->rfd, (IOHandler *)handler, NULL, e);
}

int event_notifier_set(EventNotifier *e)
{
    static const uint64_t value = 1;
    ssize_t ret;

    do {
        ret = write(e->wfd, &value, sizeof(value));
    } while (ret < 0 && errno == EINTR);

    /* EAGAIN is fine, a read must be pending.  */
    if (ret < 0 && errno != EAGAIN) {
        return -errno;
    }
    return 0;
}

int event_notifier_test_and_clear(EventNotifier *e)
{
    int value;
    ssize_t len;
    char buffer[512];

    /* Drain the notify pipe.  For eventfd, only 8 bytes will be read.  */
    value = 0;
    do {
        len = read(e->rfd, buffer, sizeof(buffer));
        value |= (len > 0);
    } while ((len == -1 && errno == EINTR) || len == sizeof(buffer));

    return value;
}
