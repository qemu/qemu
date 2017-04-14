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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/thread.h"

#include <sys/syscall.h>

struct sigfd_compat_info
{
    sigset_t mask;
    int fd;
};

static void *sigwait_compat(void *opaque)
{
    struct sigfd_compat_info *info = opaque;

    while (1) {
        int sig;
        int err;

        err = sigwait(&info->mask, &sig);
        if (err != 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return NULL;
            }
        } else {
            struct qemu_signalfd_siginfo buffer;
            size_t offset = 0;

            memset(&buffer, 0, sizeof(buffer));
            buffer.ssi_signo = sig;

            while (offset < sizeof(buffer)) {
                ssize_t len;

                len = write(info->fd, (char *)&buffer + offset,
                            sizeof(buffer) - offset);
                if (len == -1 && errno == EINTR)
                    continue;

                if (len <= 0) {
                    return NULL;
                }

                offset += len;
            }
        }
    }
}

static int qemu_signalfd_compat(const sigset_t *mask)
{
    struct sigfd_compat_info *info;
    QemuThread thread;
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

    qemu_set_cloexec(fds[0]);
    qemu_set_cloexec(fds[1]);

    memcpy(&info->mask, mask, sizeof(*mask));
    info->fd = fds[1];

    qemu_thread_create(&thread, "signalfd_compat", sigwait_compat, info,
                       QEMU_THREAD_DETACHED);

    return fds[0];
}

int qemu_signalfd(const sigset_t *mask)
{
#if defined(CONFIG_SIGNALFD)
    int ret;

    ret = syscall(SYS_signalfd, -1, mask, _NSIG / 8);
    if (ret != -1) {
        qemu_set_cloexec(ret);
        return ret;
    }
#endif

    return qemu_signalfd_compat(mask);
}
