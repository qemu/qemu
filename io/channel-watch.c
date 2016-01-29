/*
 * QEMU I/O channels watch helper APIs
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

    ssource->fd.fd = fd;
    ssource->fd.events = condition;

    g_source_add_poll(source, &ssource->fd);

    return source;
}


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

    ssource->fdread.fd = fdread;
    ssource->fdread.events = condition & G_IO_IN;

    ssource->fdwrite.fd = fdwrite;
    ssource->fdwrite.events = condition & G_IO_OUT;

    g_source_add_poll(source, &ssource->fdread);
    g_source_add_poll(source, &ssource->fdwrite);

    return source;
}
