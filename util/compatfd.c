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
#include "qemu/thread.h"

#if defined(CONFIG_SIGNALFD)
#include <sys/signalfd.h>
#endif

struct sigfd_compat_info {
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
            memset(&buffer, 0, sizeof(buffer));
            buffer.ssi_signo = sig;

            if (qemu_write_full(info->fd, &buffer, sizeof(buffer)) != sizeof(buffer)) {
                return NULL;
            }
        }
    }
}

static int qemu_signalfd_compat(const sigset_t *mask)
{
    struct sigfd_compat_info *info;
    QemuThread thread;
    int fds[2];

    info = g_malloc(sizeof(*info));

    if (!g_unix_open_pipe(fds, FD_CLOEXEC, NULL)) {
        g_free(info);
        return -1;
    }

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

    ret = signalfd(-1, mask, SFD_CLOEXEC);
    if (ret != -1) {
        return ret;
    }
#endif

    return qemu_signalfd_compat(mask);
}
