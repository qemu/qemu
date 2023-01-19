/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "net/net.h"
#include "clients.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "io/net-listener.h"
#include "qapi/qapi-events-net.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"

typedef struct NetStreamState {
    NetClientState nc;
    QIOChannel *listen_ioc;
    QIONetListener *listener;
    QIOChannel *ioc;
    guint ioc_read_tag;
    guint ioc_write_tag;
    SocketReadState rs;
    unsigned int send_index;      /* number of bytes sent*/
    uint32_t reconnect;
    guint timer_tag;
    SocketAddress *addr;
} NetStreamState;

static void net_stream_listen(QIONetListener *listener,
                              QIOChannelSocket *cioc,
                              void *opaque);
static void net_stream_arm_reconnect(NetStreamState *s);

static gboolean net_stream_writable(QIOChannel *ioc,
                                    GIOCondition condition,
                                    gpointer data)
{
    NetStreamState *s = data;

    s->ioc_write_tag = 0;

    qemu_flush_queued_packets(&s->nc);

    return G_SOURCE_REMOVE;
}

static ssize_t net_stream_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, nc, nc);
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

    remaining = iov_size(iov, 2) - s->send_index;
    nlocal_iov = iov_copy(local_iov, 2, iov, 2, s->send_index, remaining);
    ret = qio_channel_writev(s->ioc, local_iov, nlocal_iov, NULL);
    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        ret = 0; /* handled further down */
    }
    if (ret == -1) {
        s->send_index = 0;
        return -errno;
    }
    if (ret < (ssize_t)remaining) {
        s->send_index += ret;
        s->ioc_write_tag = qio_channel_add_watch(s->ioc, G_IO_OUT,
                                                 net_stream_writable, s, NULL);
        return 0;
    }
    s->send_index = 0;
    return size;
}

static gboolean net_stream_send(QIOChannel *ioc,
                                GIOCondition condition,
                                gpointer data);

static void net_stream_send_completed(NetClientState *nc, ssize_t len)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, nc, nc);

    if (!s->ioc_read_tag) {
        s->ioc_read_tag = qio_channel_add_watch(s->ioc, G_IO_IN,
                                                net_stream_send, s, NULL);
    }
}

static void net_stream_rs_finalize(SocketReadState *rs)
{
    NetStreamState *s = container_of(rs, NetStreamState, rs);

    if (qemu_send_packet_async(&s->nc, rs->buf,
                               rs->packet_len,
                               net_stream_send_completed) == 0) {
        if (s->ioc_read_tag) {
            g_source_remove(s->ioc_read_tag);
            s->ioc_read_tag = 0;
        }
    }
}

static gboolean net_stream_send(QIOChannel *ioc,
                                GIOCondition condition,
                                gpointer data)
{
    NetStreamState *s = data;
    int size;
    int ret;
    char buf1[NET_BUFSIZE];
    const char *buf;

    size = qio_channel_read(s->ioc, buf1, sizeof(buf1), NULL);
    if (size < 0) {
        if (errno != EWOULDBLOCK) {
            goto eoc;
        }
    } else if (size == 0) {
        /* end of connection */
    eoc:
        s->ioc_read_tag = 0;
        if (s->ioc_write_tag) {
            g_source_remove(s->ioc_write_tag);
            s->ioc_write_tag = 0;
        }
        if (s->listener) {
            qio_net_listener_set_client_func(s->listener, net_stream_listen,
                                             s, NULL);
        }
        object_unref(OBJECT(s->ioc));
        s->ioc = NULL;

        net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);
        s->nc.link_down = true;
        qemu_set_info_str(&s->nc, "%s", "");

        qapi_event_send_netdev_stream_disconnected(s->nc.name);
        net_stream_arm_reconnect(s);

        return G_SOURCE_REMOVE;
    }
    buf = buf1;

    ret = net_fill_rstate(&s->rs, (const uint8_t *)buf, size);

    if (ret == -1) {
        goto eoc;
    }

    return G_SOURCE_CONTINUE;
}

static void net_stream_cleanup(NetClientState *nc)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, nc, nc);
    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }
    if (s->addr) {
        qapi_free_SocketAddress(s->addr);
        s->addr = NULL;
    }
    if (s->ioc) {
        if (QIO_CHANNEL_SOCKET(s->ioc)->fd != -1) {
            if (s->ioc_read_tag) {
                g_source_remove(s->ioc_read_tag);
                s->ioc_read_tag = 0;
            }
            if (s->ioc_write_tag) {
                g_source_remove(s->ioc_write_tag);
                s->ioc_write_tag = 0;
            }
        }
        object_unref(OBJECT(s->ioc));
        s->ioc = NULL;
    }
    if (s->listen_ioc) {
        if (s->listener) {
            qio_net_listener_disconnect(s->listener);
            object_unref(OBJECT(s->listener));
            s->listener = NULL;
        }
        object_unref(OBJECT(s->listen_ioc));
        s->listen_ioc = NULL;
    }
}

static NetClientInfo net_stream_info = {
    .type = NET_CLIENT_DRIVER_STREAM,
    .size = sizeof(NetStreamState),
    .receive = net_stream_receive,
    .cleanup = net_stream_cleanup,
};

static void net_stream_listen(QIONetListener *listener,
                              QIOChannelSocket *cioc,
                              void *opaque)
{
    NetStreamState *s = opaque;
    SocketAddress *addr;
    char *uri;

    object_ref(OBJECT(cioc));

    qio_net_listener_set_client_func(s->listener, NULL, s, NULL);

    s->ioc = QIO_CHANNEL(cioc);
    qio_channel_set_name(s->ioc, "stream-server");
    s->nc.link_down = false;

    s->ioc_read_tag = qio_channel_add_watch(s->ioc, G_IO_IN, net_stream_send,
                                            s, NULL);

    if (cioc->localAddr.ss_family == AF_UNIX) {
        addr = qio_channel_socket_get_local_address(cioc, NULL);
    } else {
        addr = qio_channel_socket_get_remote_address(cioc, NULL);
    }
    g_assert(addr != NULL);
    uri = socket_uri(addr);
    qemu_set_info_str(&s->nc, "%s", uri);
    g_free(uri);
    qapi_event_send_netdev_stream_connected(s->nc.name, addr);
    qapi_free_SocketAddress(addr);
}

static void net_stream_server_listening(QIOTask *task, gpointer opaque)
{
    NetStreamState *s = opaque;
    QIOChannelSocket *listen_sioc = QIO_CHANNEL_SOCKET(s->listen_ioc);
    SocketAddress *addr;
    int ret;

    if (listen_sioc->fd < 0) {
        qemu_set_info_str(&s->nc, "connection error");
        return;
    }

    addr = qio_channel_socket_get_local_address(listen_sioc, NULL);
    g_assert(addr != NULL);
    ret = qemu_socket_try_set_nonblock(listen_sioc->fd);
    if (addr->type == SOCKET_ADDRESS_TYPE_FD && ret < 0) {
        qemu_set_info_str(&s->nc, "can't use file descriptor %s (errno %d)",
                          addr->u.fd.str, -ret);
        return;
    }
    g_assert(ret == 0);
    qapi_free_SocketAddress(addr);

    s->nc.link_down = true;
    s->listener = qio_net_listener_new();

    net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);
    qio_net_listener_set_client_func(s->listener, net_stream_listen, s, NULL);
    qio_net_listener_add(s->listener, listen_sioc);
}

static int net_stream_server_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  Error **errp)
{
    NetClientState *nc;
    NetStreamState *s;
    QIOChannelSocket *listen_sioc = qio_channel_socket_new();

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);
    s = DO_UPCAST(NetStreamState, nc, nc);

    s->listen_ioc = QIO_CHANNEL(listen_sioc);
    qio_channel_socket_listen_async(listen_sioc, addr, 0,
                                    net_stream_server_listening, s,
                                    NULL, NULL);

    return 0;
}

static void net_stream_client_connected(QIOTask *task, gpointer opaque)
{
    NetStreamState *s = opaque;
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(s->ioc);
    SocketAddress *addr;
    gchar *uri;
    int ret;

    if (sioc->fd < 0) {
        qemu_set_info_str(&s->nc, "connection error");
        goto error;
    }

    addr = qio_channel_socket_get_remote_address(sioc, NULL);
    g_assert(addr != NULL);
    uri = socket_uri(addr);
    qemu_set_info_str(&s->nc, "%s", uri);
    g_free(uri);

    ret = qemu_socket_try_set_nonblock(sioc->fd);
    if (addr->type == SOCKET_ADDRESS_TYPE_FD && ret < 0) {
        qemu_set_info_str(&s->nc, "can't use file descriptor %s (errno %d)",
                          addr->u.fd.str, -ret);
        qapi_free_SocketAddress(addr);
        goto error;
    }
    g_assert(ret == 0);

    net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);

    /* Disable Nagle algorithm on TCP sockets to reduce latency */
    qio_channel_set_delay(s->ioc, false);

    s->ioc_read_tag = qio_channel_add_watch(s->ioc, G_IO_IN, net_stream_send,
                                            s, NULL);
    s->nc.link_down = false;
    qapi_event_send_netdev_stream_connected(s->nc.name, addr);
    qapi_free_SocketAddress(addr);

    return;
error:
    object_unref(OBJECT(s->ioc));
    s->ioc = NULL;
    net_stream_arm_reconnect(s);
}

static gboolean net_stream_reconnect(gpointer data)
{
    NetStreamState *s = data;
    QIOChannelSocket *sioc;

    s->timer_tag = 0;

    sioc = qio_channel_socket_new();
    s->ioc = QIO_CHANNEL(sioc);
    qio_channel_socket_connect_async(sioc, s->addr,
                                     net_stream_client_connected, s,
                                     NULL, NULL);
    return G_SOURCE_REMOVE;
}

static void net_stream_arm_reconnect(NetStreamState *s)
{
    if (s->reconnect && s->timer_tag == 0) {
        s->timer_tag = g_timeout_add_seconds(s->reconnect,
                                             net_stream_reconnect, s);
    }
}

static int net_stream_client_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  uint32_t reconnect,
                                  Error **errp)
{
    NetStreamState *s;
    NetClientState *nc;
    QIOChannelSocket *sioc = qio_channel_socket_new();

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);
    s = DO_UPCAST(NetStreamState, nc, nc);

    s->ioc = QIO_CHANNEL(sioc);
    s->nc.link_down = true;

    s->reconnect = reconnect;
    if (reconnect) {
        s->addr = QAPI_CLONE(SocketAddress, addr);
    }
    qio_channel_socket_connect_async(sioc, addr,
                                     net_stream_client_connected, s,
                                     NULL, NULL);

    return 0;
}

int net_init_stream(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp)
{
    const NetdevStreamOptions *sock;

    assert(netdev->type == NET_CLIENT_DRIVER_STREAM);
    sock = &netdev->u.stream;

    if (!sock->has_server || !sock->server) {
        return net_stream_client_init(peer, "stream", name, sock->addr,
                                      sock->has_reconnect ? sock->reconnect : 0,
                                      errp);
    }
    if (sock->has_reconnect) {
        error_setg(errp, "'reconnect' option is incompatible with "
                         "socket in server mode");
        return -1;
    }
    return net_stream_server_init(peer, "stream", name, sock->addr, errp);
}
