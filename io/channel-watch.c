/*
 * QEMU I/O channels watch helper APIs
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
#include "io/channel-watch.h"

typedef struct QIOChannelFDSource QIOChannelFDSource;
struct QIOChannelFDSource {
    GSource parent;
    GPollFD fd;
    QIOChannel *ioc;
    GIOCondition condition;
};


#ifdef CONFIG_WIN32
typedef struct QIOChannelSocketSource QIOChannelSocketSource;
struct QIOChannelSocketSource {
    GSource parent;
    GPollFD fd;
    QIOChannel *ioc;
    SOCKET socket;
    int revents;
    GIOCondition condition;
};

#endif


typedef struct QIOChannelFDPairSource QIOChannelFDPairSource;
struct QIOChannelFDPairSource {
    GSource parent;
    GPollFD fdread;
    GPollFD fdwrite;
    QIOChannel *ioc;
    GIOCondition condition;
};


static gboolean
qio_channel_fd_source_prepare(GSource *source G_GNUC_UNUSED,
                              gint *timeout)
{
    *timeout = -1;

    return FALSE;
}


static gboolean
qio_channel_fd_source_check(GSource *source)
{
    QIOChannelFDSource *ssource = (QIOChannelFDSource *)source;

    return ssource->fd.revents & ssource->condition;
}


static gboolean
qio_channel_fd_source_dispatch(GSource *source,
                               GSourceFunc callback,
                               gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelFDSource *ssource = (QIOChannelFDSource *)source;

    return (*func)(ssource->ioc,
                   ssource->fd.revents & ssource->condition,
                   user_data);
}


static void
qio_channel_fd_source_finalize(GSource *source)
{
    QIOChannelFDSource *ssource = (QIOChannelFDSource *)source;

    object_unref(OBJECT(ssource->ioc));
}


#ifdef CONFIG_WIN32
static gboolean
qio_channel_socket_source_prepare(GSource *source G_GNUC_UNUSED,
                                  gint *timeout)
{
    *timeout = -1;

    return FALSE;
}


/*
 * NB, this impl only works when the socket is in non-blocking
 * mode on Win32
 */
static gboolean
qio_channel_socket_source_check(GSource *source)
{
    static struct timeval tv0;
    QIOChannelSocketSource *ssource = (QIOChannelSocketSource *)source;
    fd_set rfds, wfds, xfds;

    if (!ssource->condition) {
        return 0;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&xfds);
    if (ssource->condition & G_IO_IN) {
        FD_SET(ssource->socket, &rfds);
    }
    if (ssource->condition & G_IO_OUT) {
        FD_SET(ssource->socket, &wfds);
    }
    if (ssource->condition & G_IO_PRI) {
        FD_SET(ssource->socket, &xfds);
    }
    ssource->revents = 0;
    if (select(0, &rfds, &wfds, &xfds, &tv0) == 0) {
        return 0;
    }

    if (FD_ISSET(ssource->socket, &rfds)) {
        ssource->revents |= G_IO_IN;
    }
    if (FD_ISSET(ssource->socket, &wfds)) {
        ssource->revents |= G_IO_OUT;
    }
    if (FD_ISSET(ssource->socket, &xfds)) {
        ssource->revents |= G_IO_PRI;
    }

    return ssource->revents;
}


static gboolean
qio_channel_socket_source_dispatch(GSource *source,
                                   GSourceFunc callback,
                                   gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelSocketSource *ssource = (QIOChannelSocketSource *)source;

    return (*func)(ssource->ioc, ssource->revents, user_data);
}


static void
qio_channel_socket_source_finalize(GSource *source)
{
    QIOChannelSocketSource *ssource = (QIOChannelSocketSource *)source;

    object_unref(OBJECT(ssource->ioc));
}


GSourceFuncs qio_channel_socket_source_funcs = {
    qio_channel_socket_source_prepare,
    qio_channel_socket_source_check,
    qio_channel_socket_source_dispatch,
    qio_channel_socket_source_finalize
};
#endif


static gboolean
qio_channel_fd_pair_source_prepare(GSource *source G_GNUC_UNUSED,
                                   gint *timeout)
{
    *timeout = -1;

    return FALSE;
}


static gboolean
qio_channel_fd_pair_source_check(GSource *source)
{
    QIOChannelFDPairSource *ssource = (QIOChannelFDPairSource *)source;
    GIOCondition poll_condition = ssource->fdread.revents |
        ssource->fdwrite.revents;

    return poll_condition & ssource->condition;
}


static gboolean
qio_channel_fd_pair_source_dispatch(GSource *source,
                                    GSourceFunc callback,
                                    gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelFDPairSource *ssource = (QIOChannelFDPairSource *)source;
    GIOCondition poll_condition = ssource->fdread.revents |
        ssource->fdwrite.revents;

    return (*func)(ssource->ioc,
                   poll_condition & ssource->condition,
                   user_data);
}


static void
qio_channel_fd_pair_source_finalize(GSource *source)
{
    QIOChannelFDPairSource *ssource = (QIOChannelFDPairSource *)source;

    object_unref(OBJECT(ssource->ioc));
}


GSourceFuncs qio_channel_fd_source_funcs = {
    qio_channel_fd_source_prepare,
    qio_channel_fd_source_check,
    qio_channel_fd_source_dispatch,
    qio_channel_fd_source_finalize
};


GSourceFuncs qio_channel_fd_pair_source_funcs = {
    qio_channel_fd_pair_source_prepare,
    qio_channel_fd_pair_source_check,
    qio_channel_fd_pair_source_dispatch,
    qio_channel_fd_pair_source_finalize
};


GSource *qio_channel_create_fd_watch(QIOChannel *ioc,
                                     int fd,
                                     GIOCondition condition)
{
    GSource *source;
    QIOChannelFDSource *ssource;

    source = g_source_new(&qio_channel_fd_source_funcs,
                          sizeof(QIOChannelFDSource));
    ssource = (QIOChannelFDSource *)source;

    ssource->ioc = ioc;
    object_ref(OBJECT(ioc));

    ssource->condition = condition;

#ifdef CONFIG_WIN32
    ssource->fd.fd = (gint64)_get_osfhandle(fd);
#else
    ssource->fd.fd = fd;
#endif
    ssource->fd.events = condition;

    g_source_add_poll(source, &ssource->fd);

    return source;
}

#ifdef CONFIG_WIN32
GSource *qio_channel_create_socket_watch(QIOChannel *ioc,
                                         int sockfd,
                                         GIOCondition condition)
{
    GSource *source;
    QIOChannelSocketSource *ssource;

    qemu_socket_select_nofail(sockfd, ioc->event,
                              FD_READ | FD_ACCEPT | FD_CLOSE |
                              FD_CONNECT | FD_WRITE | FD_OOB);

    source = g_source_new(&qio_channel_socket_source_funcs,
                          sizeof(QIOChannelSocketSource));
    ssource = (QIOChannelSocketSource *)source;

    ssource->ioc = ioc;
    object_ref(OBJECT(ioc));

    ssource->condition = condition;
    ssource->socket = _get_osfhandle(sockfd);
    ssource->revents = 0;

    ssource->fd.fd = (gintptr)ioc->event;
    ssource->fd.events = G_IO_IN;

    g_source_add_poll(source, &ssource->fd);

    return source;
}
#else
GSource *qio_channel_create_socket_watch(QIOChannel *ioc,
                                         int socket,
                                         GIOCondition condition)
{
    return qio_channel_create_fd_watch(ioc, socket, condition);
}
#endif

GSource *qio_channel_create_fd_pair_watch(QIOChannel *ioc,
                                          int fdread,
                                          int fdwrite,
                                          GIOCondition condition)
{
    GSource *source;
    QIOChannelFDPairSource *ssource;

    source = g_source_new(&qio_channel_fd_pair_source_funcs,
                          sizeof(QIOChannelFDPairSource));
    ssource = (QIOChannelFDPairSource *)source;

    ssource->ioc = ioc;
    object_ref(OBJECT(ioc));

    ssource->condition = condition;

#ifdef CONFIG_WIN32
    ssource->fdread.fd = (gint64)_get_osfhandle(fdread);
    ssource->fdwrite.fd = (gint64)_get_osfhandle(fdwrite);
#else
    ssource->fdread.fd = fdread;
    ssource->fdwrite.fd = fdwrite;
#endif

    ssource->fdread.events = condition & G_IO_IN;
    ssource->fdwrite.events = condition & G_IO_OUT;

    g_source_add_poll(source, &ssource->fdread);
    g_source_add_poll(source, &ssource->fdwrite);

    return source;
}
