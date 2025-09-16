/*
 * net stream generic functions
 *
 * Copyright Red Hat
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qapi/error.h"
#include "net/net.h"
#include "io/channel.h"
#include "io/net-listener.h"
#include "qemu/sockets.h"

#include "stream_data.h"

static gboolean net_stream_data_writable(QIOChannel *ioc,
                                         GIOCondition condition, gpointer data)
{
    NetStreamData *d = data;

    d->ioc_write_tag = 0;

    qemu_flush_queued_packets(&d->nc);

    return G_SOURCE_REMOVE;
}

ssize_t net_stream_data_receive(NetStreamData *d, const uint8_t *buf,
                                size_t size)
{
    uint32_t len = htonl(size);
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len  = sizeof(len),
        }, {
            .iov_base = (void *)buf,
            .iov_len  = size,
        },
    };
    struct iovec local_iov[2];
    unsigned int nlocal_iov;
    size_t remaining;
    ssize_t ret;

    remaining = iov_size(iov, 2) - d->send_index;
    nlocal_iov = iov_copy(local_iov, 2, iov, 2, d->send_index, remaining);
    ret = qio_channel_writev(d->ioc, local_iov, nlocal_iov, NULL);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        ret = 0; /* handled further down */
    }
    if (ret == -1) {
        d->send_index = 0;
        return -errno;
    }
    if (ret < (ssize_t)remaining) {
        d->send_index += ret;
        d->ioc_write_tag = qio_channel_add_watch(d->ioc, G_IO_OUT,
                                                 net_stream_data_writable, d,
                                                 NULL);
        return 0;
    }
    d->send_index = 0;
    return size;
}

static void net_stream_data_send_completed(NetClientState *nc, ssize_t len)
{
    NetStreamData *d = DO_UPCAST(NetStreamData, nc, nc);

    if (!d->ioc_read_tag) {
        d->ioc_read_tag = qio_channel_add_watch(d->ioc, G_IO_IN, d->send, d,
                                                NULL);
    }
}

void net_stream_data_rs_finalize(SocketReadState *rs)
{
    NetStreamData *d = container_of(rs, NetStreamData, rs);

    if (qemu_send_packet_async(&d->nc, rs->buf,
                               rs->packet_len,
                               net_stream_data_send_completed) == 0) {
        if (d->ioc_read_tag) {
            g_source_remove(d->ioc_read_tag);
            d->ioc_read_tag = 0;
        }
    }
}

gboolean net_stream_data_send(QIOChannel *ioc, GIOCondition condition,
                              NetStreamData *d)
{
    int size;
    int ret;
    QEMU_UNINITIALIZED char buf1[NET_BUFSIZE];
    const char *buf;

    size = qio_channel_read(d->ioc, buf1, sizeof(buf1), NULL);
    if (size < 0) {
        if (errno != EWOULDBLOCK) {
            goto eoc;
        }
    } else if (size == 0) {
        /* end of connection */
    eoc:
        d->ioc_read_tag = 0;
        if (d->ioc_write_tag) {
            g_source_remove(d->ioc_write_tag);
            d->ioc_write_tag = 0;
        }
        if (d->listener) {
            qemu_set_info_str(&d->nc, "listening");
            qio_net_listener_set_client_func(d->listener,
                                             d->listen, d, NULL);
        }
        object_unref(OBJECT(d->ioc));
        d->ioc = NULL;

        net_socket_rs_init(&d->rs, net_stream_data_rs_finalize, false);
        d->nc.link_down = true;

        return G_SOURCE_REMOVE;
    }
    buf = buf1;

    ret = net_fill_rstate(&d->rs, (const uint8_t *)buf, size);

    if (ret == -1) {
        goto eoc;
    }

    return G_SOURCE_CONTINUE;
}

void net_stream_data_listen(QIONetListener *listener,
                            QIOChannelSocket *cioc,
                            NetStreamData *d)
{
    object_ref(OBJECT(cioc));

    qio_net_listener_set_client_func(d->listener, NULL, d, NULL);

    d->ioc = QIO_CHANNEL(cioc);
    qio_channel_set_name(d->ioc, "stream-server");
    d->nc.link_down = false;

    d->ioc_read_tag = qio_channel_add_watch(d->ioc, G_IO_IN, d->send, d, NULL);
}

int net_stream_data_client_connected(QIOTask *task, NetStreamData *d)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(d->ioc);
    SocketAddress *addr;
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        qemu_set_info_str(&d->nc, "error: %s", error_get_pretty(err));
        error_free(err);
        goto error;
    }

    addr = qio_channel_socket_get_remote_address(sioc, NULL);
    g_assert(addr != NULL);

    if (!qemu_set_blocking(sioc->fd, false, &err)) {
        qemu_set_info_str(&d->nc, "error: %s", error_get_pretty(err));
        error_free(err);
        qapi_free_SocketAddress(addr);
        goto error;
    }
    qapi_free_SocketAddress(addr);

    net_socket_rs_init(&d->rs, net_stream_data_rs_finalize, false);

    /* Disable Nagle algorithm on TCP sockets to reduce latency */
    qio_channel_set_delay(d->ioc, false);

    d->ioc_read_tag = qio_channel_add_watch(d->ioc, G_IO_IN, d->send, d, NULL);
    d->nc.link_down = false;

    return 0;
error:
    object_unref(OBJECT(d->ioc));
    d->ioc = NULL;

    return -1;
}
