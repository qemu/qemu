/*
 * signalfd/eventfd compatibility
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "compatfd.h"

#include <sys/syscall.h>
#include <pthread.h>

struct sigfd_compat_info
{
    sigset_t mask;
    int fd;
};

static void *sigwait_compat(void *opaque)
{
    struct sigfd_compat_info *info = opaque;
    int err;
    sigset_t all;

    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, NULL);

    do {
        siginfo_t siginfo;

        err = sigwaitinfo(&info->mask, &siginfo);
        if (err == -1 && errno == EINTR) {
            err = 0;
            continue;
        }

        if (err > 0) {
            char buffer[128];
            size_t offset = 0;

            memcpy(buffer, &err, sizeof(err));
            while (offset < sizeof(buffer)) {
                ssize_t len;

                len = write(info->fd, buffer + offset,
                            sizeof(buffer) - offset);
                if (len == -1 && errno == EINTR)
                    continue;

                if (len <= 0) {
                    err = -1;
                    break;
                }

                offset += len;
            }
        }
    } while (err >= 0);

    return NULL;
}

static int qemu_signalfd_compat(const sigset_t *mask)
{
    pthread_attr_t attr;
    pthread_t tid;
    struct sigfd_compat_info *info;
    int fds[2];

    info = malloc(sizeof(*info));
    if (info == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (pipe(fds) == -1) {
        free(info);
        return -1;
    }

    memcpy(&info->mask, mask, sizeof(*mask));
    info->fd = fds[1];

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&tid, &attr, sigwait_compat, info);

    pthread_attr_destroy(&attr);

    return fds[0];
}

int qemu_signalfd(const sigset_t *mask)
{
#if defined(CONFIG_signalfd)
    int ret;

    ret = syscall(SYS_signalfd, -1, mask, _NSIG / 8);
    if (ret != -1)
        return ret;
#endif

    return qemu_signalfd_compat(mask);
}

int qemu_eventfd(int *fds)
{
#if defined(CONFIG_eventfd)
    int ret;

    ret = syscall(SYS_eventfd, 0);
    if (ret >= 0) {
        fds[0] = fds[1] = ret;
        return 0;
    }
#endif

    return pipe(fds);
}
