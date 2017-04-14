/*
 * systemd socket activation support
 *
 * Copyright 2017 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Richard W.M. Jones <rjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/systemd.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"

#ifndef _WIN32
unsigned int check_socket_activation(void)
{
    const char *s;
    unsigned long pid;
    unsigned long nr_fds;
    unsigned int i;
    int fd;
    int err;

    s = getenv("LISTEN_PID");
    if (s == NULL) {
        return 0;
    }
    err = qemu_strtoul(s, NULL, 10, &pid);
    if (err) {
        return 0;
    }
    if (pid != getpid()) {
        return 0;
    }

    s = getenv("LISTEN_FDS");
    if (s == NULL) {
        return 0;
    }
    err = qemu_strtoul(s, NULL, 10, &nr_fds);
    if (err) {
        return 0;
    }
    assert(nr_fds <= UINT_MAX);

    /* So these are not passed to any child processes we might start. */
    unsetenv("LISTEN_FDS");
    unsetenv("LISTEN_PID");

    /* So the file descriptors don't leak into child processes. */
    for (i = 0; i < nr_fds; ++i) {
        fd = FIRST_SOCKET_ACTIVATION_FD + i;
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
            /* If we cannot set FD_CLOEXEC then it probably means the file
             * descriptor is invalid, so socket activation has gone wrong
             * and we should exit.
             */
            error_report("Socket activation failed: "
                         "invalid file descriptor fd = %d: %m",
                         fd);
            exit(EXIT_FAILURE);
        }
    }

    return (unsigned int) nr_fds;
}

#else /* !_WIN32 */
unsigned int check_socket_activation(void)
{
    return 0;
}
#endif
