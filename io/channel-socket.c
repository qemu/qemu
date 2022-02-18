/*
 * QEMU I/O channels sockets driver
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
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qemu/module.h"
#include "io/channel-socket.h"
#include "io/channel-watch.h"
#include "trace.h"
#include "qapi/clone-visitor.h"

#define SOCKET_MAX_FDS 16

SocketAddress *
qio_channel_socket_get_local_address(QIOChannelSocket *ioc,
                                     Error **errp)
{
    return socket_sockaddr_to_address(&ioc->localAddr,
                                      ioc->localAddrLen,
                                      errp);
}

SocketAddress *
qio_channel_socket_get_remote_address(QIOChannelSocket *ioc,
                                      Error **errp)
{
    return socket_sockaddr_to_address(&ioc->remoteAddr,
                                      ioc->remoteAddrLen,
                                      errp);
}

QIOChannelSocket *
qio_channel_socket_new(void)
{
    QIOChannelSocket *sioc;
    QIOChannel *ioc;

    sioc = QIO_CHANNEL_SOCKET(object_new(TYPE_QIO_CHANNEL_SOCKET));
    sioc->fd = -1;

    ioc = QIO_CHANNEL(sioc);
    qio_channel_set_feature(ioc, QIO_CHANNEL_FEATURE_SHUTDOWN);

#ifdef WIN32
    ioc->event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

    trace_qio_channel_socket_new(sioc);

    return sioc;
}


static int
qio_channel_socket_set_fd(QIOChannelSocket *sioc,
                          int fd,
                          Error **errp)
{
    if (sioc->fd != -1) {
        error_setg(errp, "Socket is already open");
        return -1;
    }

    sioc->fd = fd;
    sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
    sioc->localAddrLen = sizeof(sioc->localAddr);


    if (getpeername(fd, (struct sockaddr *)&sioc->remoteAddr,
                    &sioc->remoteAddrLen) < 0) {
        if (errno == ENOTCONN) {
            memset(&sioc->remoteAddr, 0, sizeof(sioc->remoteAddr));
            sioc->remoteAddrLen = sizeof(sioc->remoteAddr);
        } else {
            error_setg_errno(errp, errno,
                             "Unable to query remote socket address");
            goto error;
        }
    }

    if (getsockname(fd, (struct sockaddr *)&sioc->localAddr,
                    &sioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to query local socket address");
        goto error;
    }

#ifndef WIN32
    if (sioc->localAddr.ss_family == AF_UNIX) {
        QIOChannel *ioc = QIO_CHANNEL(sioc);
        qio_channel_set_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS);
    }
#endif /* WIN32 */

    return 0;

 error:
    sioc->fd = -1; /* Let the caller close FD on failure */
    return -1;
}

QIOChannelSocket *
qio_channel_socket_new_fd(int fd,
                          Error **errp)
{
    QIOChannelSocket *ioc;

    ioc = qio_channel_socket_new();
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) {
        object_unref(OBJECT(ioc));
        return NULL;
    }

    trace_qio_channel_socket_new_fd(ioc, fd);

    return ioc;
}


int qio_channel_socket_connect_sync(QIOChannelSocket *ioc,
                                    SocketAddress *addr,
                                    Error **errp)
{
    int fd;

    trace_qio_channel_socket_connect_sync(ioc, addr);
    fd = socket_connect(addr, errp);
    if (fd < 0) {
        trace_qio_channel_socket_connect_fail(ioc);
        return -1;
    }

    trace_qio_channel_socket_connect_complete(ioc, fd);
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) {
        close(fd);
        return -1;
    }

    return 0;
}


static void qio_channel_socket_connect_worker(QIOTask *task,
                                              gpointer opaque)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    SocketAddress *addr = opaque;
    Error *err = NULL;

    qio_channel_socket_connect_sync(ioc, addr, &err);

    qio_task_set_error(task, err);
}


void qio_channel_socket_connect_async(QIOChannelSocket *ioc,
                                      SocketAddress *addr,
                                      QIOTaskFunc callback,
                                      gpointer opaque,
                                      GDestroyNotify destroy,
                                      GMainContext *context)
{
    QIOTask *task = qio_task_new(
        OBJECT(ioc), callback, opaque, destroy);
    SocketAddress *addrCopy;

    addrCopy = QAPI_CLONE(SocketAddress, addr);

    /* socket_connect() does a non-blocking connect(), but it
     * still blocks in DNS lookups, so we must use a thread */
    trace_qio_channel_socket_connect_async(ioc, addr);
    qio_task_run_in_thread(task,
                           qio_channel_socket_connect_worker,
                           addrCopy,
                           (GDestroyNotify)qapi_free_SocketAddress,
                           context);
}


int qio_channel_socket_listen_sync(QIOChannelSocket *ioc,
                                   SocketAddress *addr,
                                   int num,
                                   Error **errp)
{
    int fd;

    trace_qio_channel_socket_listen_sync(ioc, addr, num);
    fd = socket_listen(addr, num, errp);
    if (fd < 0) {
        trace_qio_channel_socket_listen_fail(ioc);
        return -1;
    }

    trace_qio_channel_socket_listen_complete(ioc, fd);
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) {
        close(fd);
        return -1;
    }
    qio_channel_set_feature(QIO_CHANNEL(ioc), QIO_CHANNEL_FEATURE_LISTEN);

    return 0;
}


struct QIOChannelListenWorkerData {
    SocketAddress *addr;
    int num; /* amount of expected connections */
};

static void qio_channel_listen_worker_free(gpointer opaque)
{
    struct QIOChannelListenWorkerData *data = opaque;

    qapi_free_SocketAddress(data->addr);
    g_free(data);
}

static void qio_channel_socket_listen_worker(QIOTask *task,
                                             gpointer opaque)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    struct QIOChannelListenWorkerData *data = opaque;
    Error *err = NULL;

    qio_channel_socket_listen_sync(ioc, data->addr, data->num, &err);

    qio_task_set_error(task, err);
}


void qio_channel_socket_listen_async(QIOChannelSocket *ioc,
                                     SocketAddress *addr,
                                     int num,
                                     QIOTaskFunc callback,
                                     gpointer opaque,
                                     GDestroyNotify destroy,
                                     GMainContext *context)
{
    QIOTask *task = qio_task_new(
        OBJECT(ioc), callback, opaque, destroy);
    struct QIOChannelListenWorkerData *data;

    data = g_new0(struct QIOChannelListenWorkerData, 1);
    data->addr = QAPI_CLONE(SocketAddress, addr);
    data->num = num;

    /* socket_listen() blocks in DNS lookups, so we must use a thread */
    trace_qio_channel_socket_listen_async(ioc, addr, num);
    qio_task_run_in_thread(task,
                           qio_channel_socket_listen_worker,
                           data,
                           qio_channel_listen_worker_free,
                           context);
}


int qio_channel_socket_dgram_sync(QIOChannelSocket *ioc,
                                  SocketAddress *localAddr,
                                  SocketAddress *remoteAddr,
                                  Error **errp)
{
    int fd;

    trace_qio_channel_socket_dgram_sync(ioc, localAddr, remoteAddr);
    fd = socket_dgram(remoteAddr, localAddr, errp);
    if (fd < 0) {
        trace_qio_channel_socket_dgram_fail(ioc);
        return -1;
    }

    trace_qio_channel_socket_dgram_complete(ioc, fd);
    if (qio_channel_socket_set_fd(ioc, fd, errp) < 0) {
        close(fd);
        return -1;
    }

    return 0;
}


struct QIOChannelSocketDGramWorkerData {
    SocketAddress *localAddr;
    SocketAddress *remoteAddr;
};


static void qio_channel_socket_dgram_worker_free(gpointer opaque)
{
    struct QIOChannelSocketDGramWorkerData *data = opaque;
    qapi_free_SocketAddress(data->localAddr);
    qapi_free_SocketAddress(data->remoteAddr);
    g_free(data);
}

static void qio_channel_socket_dgram_worker(QIOTask *task,
                                            gpointer opaque)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    struct QIOChannelSocketDGramWorkerData *data = opaque;
    Error *err = NULL;

    /* socket_dgram() blocks in DNS lookups, so we must use a thread */
    qio_channel_socket_dgram_sync(ioc, data->localAddr,
                                  data->remoteAddr, &err);

    qio_task_set_error(task, err);
}


void qio_channel_socket_dgram_async(QIOChannelSocket *ioc,
                                    SocketAddress *localAddr,
                                    SocketAddress *remoteAddr,
                                    QIOTaskFunc callback,
                                    gpointer opaque,
                                    GDestroyNotify destroy,
                                    GMainContext *context)
{
    QIOTask *task = qio_task_new(
        OBJECT(ioc), callback, opaque, destroy);
    struct QIOChannelSocketDGramWorkerData *data = g_new0(
        struct QIOChannelSocketDGramWorkerData, 1);

    data->localAddr = QAPI_CLONE(SocketAddress, localAddr);
    data->remoteAddr = QAPI_CLONE(SocketAddress, remoteAddr);

    trace_qio_channel_socket_dgram_async(ioc, localAddr, remoteAddr);
    qio_task_run_in_thread(task,
                           qio_channel_socket_dgram_worker,
                           data,
                           qio_channel_socket_dgram_worker_free,
                           context);
}


QIOChannelSocket *
qio_channel_socket_accept(QIOChannelSocket *ioc,
                          Error **errp)
{
    QIOChannelSocket *cioc;

    cioc = qio_channel_socket_new();
    cioc->remoteAddrLen = sizeof(ioc->remoteAddr);
    cioc->localAddrLen = sizeof(ioc->localAddr);

 retry:
    trace_qio_channel_socket_accept(ioc);
    cioc->fd = qemu_accept(ioc->fd, (struct sockaddr *)&cioc->remoteAddr,
                           &cioc->remoteAddrLen);
    if (cioc->fd < 0) {
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno, "Unable to accept connection");
        trace_qio_channel_socket_accept_fail(ioc);
        goto error;
    }

    if (getsockname(cioc->fd, (struct sockaddr *)&cioc->localAddr,
                    &cioc->localAddrLen) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to query local socket address");
        goto error;
    }

#ifndef WIN32
    if (cioc->localAddr.ss_family == AF_UNIX) {
        QIOChannel *ioc_local = QIO_CHANNEL(cioc);
        qio_channel_set_feature(ioc_local, QIO_CHANNEL_FEATURE_FD_PASS);
    }
#endif /* WIN32 */

    trace_qio_channel_socket_accept_complete(ioc, cioc, cioc->fd);
    return cioc;

 error:
    object_unref(OBJECT(cioc));
    return NULL;
}

static void qio_channel_socket_init(Object *obj)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(obj);
    ioc->fd = -1;
}

static void qio_channel_socket_finalize(Object *obj)
{
    QIOChannelSocket *ioc = QIO_CHANNEL_SOCKET(obj);

    if (ioc->fd != -1) {
        QIOChannel *ioc_local = QIO_CHANNEL(ioc);
        if (qio_channel_has_feature(ioc_local, QIO_CHANNEL_FEATURE_LISTEN)) {
            Error *err = NULL;

            socket_listen_cleanup(ioc->fd, &err);
            if (err) {
                error_report_err(err);
                err = NULL;
            }
        }
#ifdef WIN32
        WSAEventSelect(ioc->fd, NULL, 0);
#endif
        closesocket(ioc->fd);
        ioc->fd = -1;
    }
}


#ifndef WIN32
static void qio_channel_socket_copy_fds(struct msghdr *msg,
                                        int **fds, size_t *nfds)
{
    struct cmsghdr *cmsg;

    *nfds = 0;
    *fds = NULL;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        int fd_size, i;
        int gotfds;

        if (cmsg->cmsg_len < CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        fd_size = cmsg->cmsg_len - CMSG_LEN(0);

        if (!fd_size) {
            continue;
        }

        gotfds = fd_size / sizeof(int);
        *fds = g_renew(int, *fds, *nfds + gotfds);
        memcpy(*fds + *nfds, CMSG_DATA(cmsg), fd_size);

        for (i = 0; i < gotfds; i++) {
            int fd = (*fds)[*nfds + i];
            if (fd < 0) {
                continue;
            }

            /* O_NONBLOCK is preserved across SCM_RIGHTS so reset it */
            qemu_set_block(fd);

#ifndef MSG_CMSG_CLOEXEC
            qemu_set_cloexec(fd);
#endif
        }
        *nfds += gotfds;
    }
}


static ssize_t qio_channel_socket_readv(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int **fds,
                                        size_t *nfds,
                                        Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t ret;
    struct msghdr msg = { NULL, };
    char control[CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS)];
    int sflags = 0;

    memset(control, 0, CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS));

    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = niov;
    if (fds && nfds) {
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
#ifdef MSG_CMSG_CLOEXEC
        sflags |= MSG_CMSG_CLOEXEC;
#endif

    }

 retry:
    ret = recvmsg(sioc->fd, &msg, sflags);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }

        error_setg_errno(errp, errno,
                         "Unable to read from socket");
        return -1;
    }

    if (fds && nfds) {
        qio_channel_socket_copy_fds(&msg, fds, nfds);
    }

    return ret;
}

static ssize_t qio_channel_socket_writev(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int *fds,
                                         size_t nfds,
                                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t ret;
    struct msghdr msg = { NULL, };
    char control[CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS)];
    size_t fdsize = sizeof(int) * nfds;
    struct cmsghdr *cmsg;

    memset(control, 0, CMSG_SPACE(sizeof(int) * SOCKET_MAX_FDS));

    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = niov;

    if (nfds) {
        if (nfds > SOCKET_MAX_FDS) {
            error_setg_errno(errp, EINVAL,
                             "Only %d FDs can be sent, got %zu",
                             SOCKET_MAX_FDS, nfds);
            return -1;
        }

        msg.msg_control = control;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(fdsize);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), fds, fdsize);
    }

 retry:
    ret = sendmsg(sioc->fd, &msg, 0);
    if (ret <= 0) {
        if (errno == EAGAIN) {
            return QIO_CHANNEL_ERR_BLOCK;
        }
        if (errno == EINTR) {
            goto retry;
        }
        error_setg_errno(errp, errno,
                         "Unable to write to socket");
        return -1;
    }
    return ret;
}
#else /* WIN32 */
static ssize_t qio_channel_socket_readv(QIOChannel *ioc,
                                        const struct iovec *iov,
                                        size_t niov,
                                        int **fds,
                                        size_t *nfds,
                                        Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t done = 0;
    ssize_t i;

    for (i = 0; i < niov; i++) {
        ssize_t ret;
    retry:
        ret = recv(sioc->fd,
                   iov[i].iov_base,
                   iov[i].iov_len,
                   0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                if (done) {
                    return done;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            } else if (errno == EINTR) {
                goto retry;
            } else {
                error_setg_errno(errp, errno,
                                 "Unable to read from socket");
                return -1;
            }
        }
        done += ret;
        if (ret < iov[i].iov_len) {
            return done;
        }
    }

    return done;
}

static ssize_t qio_channel_socket_writev(QIOChannel *ioc,
                                         const struct iovec *iov,
                                         size_t niov,
                                         int *fds,
                                         size_t nfds,
                                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    ssize_t done = 0;
    ssize_t i;

    for (i = 0; i < niov; i++) {
        ssize_t ret;
    retry:
        ret = send(sioc->fd,
                   iov[i].iov_base,
                   iov[i].iov_len,
                   0);
        if (ret < 0) {
            if (errno == EAGAIN) {
                if (done) {
                    return done;
                } else {
                    return QIO_CHANNEL_ERR_BLOCK;
                }
            } else if (errno == EINTR) {
                goto retry;
            } else {
                error_setg_errno(errp, errno,
                                 "Unable to write to socket");
                return -1;
            }
        }
        done += ret;
        if (ret < iov[i].iov_len) {
            return done;
        }
    }

    return done;
}
#endif /* WIN32 */

static int
qio_channel_socket_set_blocking(QIOChannel *ioc,
                                bool enabled,
                                Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);

    if (enabled) {
        qemu_set_block(sioc->fd);
    } else {
        qemu_set_nonblock(sioc->fd);
    }
    return 0;
}


static void
qio_channel_socket_set_delay(QIOChannel *ioc,
                             bool enabled)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int v = enabled ? 0 : 1;

    setsockopt(sioc->fd,
               IPPROTO_TCP, TCP_NODELAY,
               &v, sizeof(v));
}


static void
qio_channel_socket_set_cork(QIOChannel *ioc,
                            bool enabled)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int v = enabled ? 1 : 0;

    socket_set_cork(sioc->fd, v);
}


static int
qio_channel_socket_close(QIOChannel *ioc,
                         Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int rc = 0;
    Error *err = NULL;

    if (sioc->fd != -1) {
#ifdef WIN32
        WSAEventSelect(sioc->fd, NULL, 0);
#endif
        if (qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_LISTEN)) {
            socket_listen_cleanup(sioc->fd, errp);
        }

        if (closesocket(sioc->fd) < 0) {
            sioc->fd = -1;
            error_setg_errno(&err, errno, "Unable to close socket");
            error_propagate(errp, err);
            return -1;
        }
        sioc->fd = -1;
    }
    return rc;
}

static int
qio_channel_socket_shutdown(QIOChannel *ioc,
                            QIOChannelShutdown how,
                            Error **errp)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    int sockhow;

    switch (how) {
    case QIO_CHANNEL_SHUTDOWN_READ:
        sockhow = SHUT_RD;
        break;
    case QIO_CHANNEL_SHUTDOWN_WRITE:
        sockhow = SHUT_WR;
        break;
    case QIO_CHANNEL_SHUTDOWN_BOTH:
    default:
        sockhow = SHUT_RDWR;
        break;
    }

    if (shutdown(sioc->fd, sockhow) < 0) {
        error_setg_errno(errp, errno,
                         "Unable to shutdown socket");
        return -1;
    }
    return 0;
}

static void qio_channel_socket_set_aio_fd_handler(QIOChannel *ioc,
                                                  AioContext *ctx,
                                                  IOHandler *io_read,
                                                  IOHandler *io_write,
                                                  void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    aio_set_fd_handler(ctx, sioc->fd, false,
                       io_read, io_write, NULL, NULL, opaque);
}

static GSource *qio_channel_socket_create_watch(QIOChannel *ioc,
                                                GIOCondition condition)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(ioc);
    return qio_channel_create_socket_watch(ioc,
                                           sioc->fd,
                                           condition);
}

static void qio_channel_socket_class_init(ObjectClass *klass,
                                          void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_socket_writev;
    ioc_klass->io_readv = qio_channel_socket_readv;
    ioc_klass->io_set_blocking = qio_channel_socket_set_blocking;
    ioc_klass->io_close = qio_channel_socket_close;
    ioc_klass->io_shutdown = qio_channel_socket_shutdown;
    ioc_klass->io_set_cork = qio_channel_socket_set_cork;
    ioc_klass->io_set_delay = qio_channel_socket_set_delay;
    ioc_klass->io_create_watch = qio_channel_socket_create_watch;
    ioc_klass->io_set_aio_fd_handler = qio_channel_socket_set_aio_fd_handler;
}

static const TypeInfo qio_channel_socket_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_QIO_CHANNEL_SOCKET,
    .instance_size = sizeof(QIOChannelSocket),
    .instance_init = qio_channel_socket_init,
    .instance_finalize = qio_channel_socket_finalize,
    .class_init = qio_channel_socket_class_init,
};

static void qio_channel_socket_register_types(void)
{
    type_register_static(&qio_channel_socket_info);
}

type_init(qio_channel_socket_register_types);
