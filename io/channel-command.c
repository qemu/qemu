/*
 * QEMU I/O channels external command driver
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/channel-command.h"
#include "io/channel-watch.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "trace.h"

/**
 * qio_channel_command_new_pid:
 * @writefd: the FD connected to the command's stdin
 * @readfd: the FD connected to the command's stdout
 * @pid: the PID/HANDLE of the running child command
 * @errp: pointer to a NULL-initialized error object
 *
 * Create a channel for performing I/O with the
 * previously spawned command identified by @pid.
 * The two file descriptors provide the connection
 * to command's stdio streams, either one or which
 * may be -1 to indicate that stream is not open.
 *
 * The channel will take ownership of the process
 * @pid and will kill it when closing the channel.
 * Similarly it will take responsibility for
 * closing the file descriptors @writefd and @readfd.
 *
 * Returns: the command channel object, or NULL on error
 */
static QIOChannelCommand *
qio_channel_command_new_pid(int writefd,
                            int readfd,
                            GPid pid)
{
    QIOChannelCommand *ioc;

    ioc = QIO_CHANNEL_COMMAND(object_new(TYPE_QIO_CHANNEL_COMMAND));

    ioc->readfd = readfd;
    ioc->writefd = writefd;
    ioc->pid = pid;

    trace_qio_channel_command_new_pid(ioc, writefd, readfd,
#ifdef WIN32
                                      GetProcessId(pid)
#else
                                      pid
#endif
        );
    return ioc;
}

QIOChannelCommand *
qio_channel_command_new_spawn(const char *const argv[],
                              int flags,
                              Error **errp)
{
    g_autoptr(GError) err = NULL;
    GPid pid = 0;
    GSpawnFlags gflags = G_SPAWN_CLOEXEC_PIPES | G_SPAWN_DO_NOT_REAP_CHILD;
    int stdinfd = -1, stdoutfd = -1;

    flags = flags & O_ACCMODE;
    gflags |= flags == O_WRONLY ? G_SPAWN_STDOUT_TO_DEV_NULL : 0;

    if (!g_spawn_async_with_pipes(NULL, (char **)argv, NULL, gflags, NULL, NULL,
                                  &pid,
                                  flags == O_RDONLY ? NULL : &stdinfd,
                                  flags == O_WRONLY ? NULL : &stdoutfd,
                                  NULL, &err)) {
        error_setg(errp, "%s", err->message);
        return NULL;
    }

    return qio_channel_command_new_pid(stdinfd, stdoutfd, pid);
}

#ifndef WIN32
static int qio_channel_command_abort(QIOChannelCommand *ioc,
                                     Error **errp)
{
    pid_t ret;
    int status;
    int step = 0;

    /* See if intermediate process has exited; if not, try a nice
     * SIGTERM followed by a more severe SIGKILL.
     */
 rewait:
    trace_qio_channel_command_abort(ioc, ioc->pid);
    ret = waitpid(ioc->pid, &status, WNOHANG);
    trace_qio_channel_command_wait(ioc, ioc->pid, ret, status);
    if (ret == (pid_t)-1) {
        if (errno == EINTR) {
            goto rewait;
        } else {
            error_setg_errno(errp, errno,
                             "Cannot wait on pid %llu",
                             (unsigned long long)ioc->pid);
            return -1;
        }
    } else if (ret == 0) {
        if (step == 0) {
            kill(ioc->pid, SIGTERM);
        } else if (step == 1) {
            kill(ioc->pid, SIGKILL);
        } else {
            error_setg(errp,
                       "Process %llu refused to die",
                       (unsigned long long)ioc->pid);
            return -1;
        }
        step++;
        usleep(10 * 1000);
        goto rewait;
    }

    return 0;
}
#else
static int qio_channel_command_abort(QIOChannelCommand *ioc,
                                     Error **errp)
{
    DWORD ret;

    TerminateProcess(ioc->pid, 0);
    ret = WaitForSingleObject(ioc->pid, 1000);
    if (ret != WAIT_OBJECT_0) {
        error_setg(errp,
                   "Process %llu refused to die",
                   (unsigned long long)GetProcessId(ioc->pid));
        return -1;
    }

    return 0;
}
#endif /* ! WIN32 */


static void qio_channel_command_init(Object *obj)
{
    QIOChannelCommand *ioc = QIO_CHANNEL_COMMAND(obj);
    ioc->readfd = -1;
    ioc->writefd = -1;
    ioc->pid = 0;
}

static void qio_channel_command_finalize(Object *obj)
{
    QIOChannelCommand *ioc = QIO_CHANNEL_COMMAND(obj);
    if (ioc->readfd != -1) {
        close(ioc->readfd);
    }
    if (ioc->writefd != -1 &&
        ioc->writefd != ioc->readfd) {
        close(ioc->writefd);
    }
    ioc->writefd = ioc->readfd = -1;
    if (ioc->pid > 0) {
        qio_channel_command_abort(ioc, NULL);
        g_spawn_close_pid(ioc->pid);
    }
}

#ifdef WIN32
static bool win32_fd_poll(int fd, gushort events)
{
    GPollFD pfd = { .fd = _get_osfhandle(fd), .events = events };
    int res;

    do {
        res = g_poll(&pfd, 1, 0);
    } while (res < 0 && errno == EINTR);
    if (res == 0) {
        return false;
    }

    return true;
}
#endif

static ssize_t qio_channel_command_readv(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int **fds,
                                         size_t *nfds,
                                         int flags,
                                         Error **errp)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);
    ssize_t ret;

#ifdef WIN32
    if (!cioc->blocking && !win32_fd_poll(cioc->readfd, G_IO_IN)) {
        return QIO_CHANNEL_ERR_BLOCK;
    }
#endif

 retry:
    ret = readv(cioc->readfd, iov, niov);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }

        error_setg_errno(errp, errno,
                         "Unable to read from command");
        return -1;
    }

    return ret;
}

static ssize_t qio_channel_command_writev(QIOChannel *ioc,
                                          const struct iovec *iov,
                                          size_t niov,
                                          int *fds,
                                          size_t nfds,
                                          int flags,
                                          Error **errp)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);
    ssize_t ret;

#ifdef WIN32
    if (!cioc->blocking && !win32_fd_poll(cioc->writefd, G_IO_OUT)) {
        return QIO_CHANNEL_ERR_BLOCK;
    }
#endif

 retry:
    ret = writev(cioc->writefd, iov, niov);
    if (ret <= 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "%s",
                         "Unable to write to command");
        return -1;
    }
    return ret;
}

static int qio_channel_command_set_blocking(QIOChannel *ioc,
                                            bool enabled,
                                            Error **errp)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);

#ifdef WIN32
    cioc->blocking = enabled;
#else

    if ((cioc->writefd >= 0 && !g_unix_set_fd_nonblocking(cioc->writefd, !enabled, NULL)) ||
        (cioc->readfd >= 0 && !g_unix_set_fd_nonblocking(cioc->readfd, !enabled, NULL))) {
        error_setg_errno(errp, errno, "Failed to set FD nonblocking");
        return -1;
    }
#endif
    return 0;
}


static int qio_channel_command_close(QIOChannel *ioc,
                                     Error **errp)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);
    int rv = 0;
#ifndef WIN32
    pid_t wp;
#endif

    /* We close FDs before killing, because that
     * gives a better chance of clean shutdown
     */
    if (cioc->readfd != -1 &&
        close(cioc->readfd) < 0) {
        rv = -1;
    }
    if (cioc->writefd != -1 &&
        cioc->writefd != cioc->readfd &&
        close(cioc->writefd) < 0) {
        rv = -1;
    }
    cioc->writefd = cioc->readfd = -1;

#ifndef WIN32
    do {
        wp = waitpid(cioc->pid, NULL, 0);
    } while (wp == (pid_t)-1 && errno == EINTR);
    if (wp == (pid_t)-1) {
        error_setg_errno(errp, errno, "Failed to wait for pid %llu",
                         (unsigned long long)cioc->pid);
        return -1;
    }
#else
    WaitForSingleObject(cioc->pid, INFINITE);
#endif

    if (rv < 0) {
        error_setg_errno(errp, errno, "%s",
                         "Unable to close command");
    }
    return rv;
}


static void qio_channel_command_set_aio_fd_handler(QIOChannel *ioc,
                                                   AioContext *ctx,
                                                   IOHandler *io_read,
                                                   IOHandler *io_write,
                                                   void *opaque)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);
    aio_set_fd_handler(ctx, cioc->readfd, io_read, NULL, NULL, NULL, opaque);
    aio_set_fd_handler(ctx, cioc->writefd, NULL, io_write, NULL, NULL, opaque);
}


static GSource *qio_channel_command_create_watch(QIOChannel *ioc,
                                                 GIOCondition condition)
{
    QIOChannelCommand *cioc = QIO_CHANNEL_COMMAND(ioc);
    return qio_channel_create_fd_pair_watch(ioc,
                                            cioc->readfd,
                                            cioc->writefd,
                                            condition);
}


static void qio_channel_command_class_init(ObjectClass *klass,
                                           void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_command_writev;
    ioc_klass->io_readv = qio_channel_command_readv;
    ioc_klass->io_set_blocking = qio_channel_command_set_blocking;
    ioc_klass->io_close = qio_channel_command_close;
    ioc_klass->io_create_watch = qio_channel_command_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_command_set_aio_fd_handler;
}

static const TypeInfo qio_channel_command_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_COMMAND,
    .instance_size = sizeof(QIOChannelCommand),
    .instance_init = qio_channel_command_init,
    .instance_finalize = qio_channel_command_finalize,
    .class_init = qio_channel_command_class_init,
};

static void qio_channel_command_register_types(void)
{
    type_register_static(&qio_channel_command_info);
}

type_init(qio_channel_command_register_types);
