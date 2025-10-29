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
#include "qapi/error.h"
#include "io/net-listener.h"
#include "qapi/qapi-events-net.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/clone-visitor.h"

#include "stream_data.h"

typedef struct NetStreamState {
    NetStreamData data;
    uint32_t reconnect_ms;
    guint timer_tag;
    SocketAddress *addr;
} NetStreamState;

static void net_stream_arm_reconnect(NetStreamState *s);

static ssize_t net_stream_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    NetStreamData *d = DO_UPCAST(NetStreamData, nc, nc);

    return net_stream_data_receive(d, buf, size);
}

static gboolean net_stream_send(QIOChannel *ioc,
                                GIOCondition condition,
                                gpointer data)
{
    if (net_stream_data_send(ioc, condition, data) == G_SOURCE_REMOVE) {
        NetStreamState *s = DO_UPCAST(NetStreamState, data, data);

        qapi_event_send_netdev_stream_disconnected(s->data.nc.name);
        net_stream_arm_reconnect(s);

        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void net_stream_cleanup(NetClientState *nc)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, data.nc, nc);
    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }
    if (s->addr) {
        qapi_free_SocketAddress(s->addr);
        s->addr = NULL;
    }
    if (s->data.ioc) {
        if (QIO_CHANNEL_SOCKET(s->data.ioc)->fd != -1) {
            if (s->data.ioc_read_tag) {
                g_source_remove(s->data.ioc_read_tag);
                s->data.ioc_read_tag = 0;
            }
            if (s->data.ioc_write_tag) {
                g_source_remove(s->data.ioc_write_tag);
                s->data.ioc_write_tag = 0;
            }
        }
        object_unref(OBJECT(s->data.ioc));
        s->data.ioc = NULL;
    }
    if (s->data.listen_ioc) {
        if (s->data.listener) {
            qio_net_listener_disconnect(s->data.listener);
            object_unref(OBJECT(s->data.listener));
            s->data.listener = NULL;
        }
        object_unref(OBJECT(s->data.listen_ioc));
        s->data.listen_ioc = NULL;
    }
}

static NetClientInfo net_stream_info = {
    .type = NET_CLIENT_DRIVER_STREAM,
    .size = sizeof(NetStreamState),
    .receive = net_stream_receive,
    .cleanup = net_stream_cleanup,
};

static void net_stream_listen(QIONetListener *listener,
                                  QIOChannelSocket *cioc, gpointer data)
{
    NetStreamData *d = data;
    SocketAddress *addr;
    char *uri;

    net_stream_data_listen(listener, cioc, data);

    if (cioc->localAddr.ss_family == AF_UNIX) {
        addr = qio_channel_socket_get_local_address(cioc, NULL);
    } else {
        addr = qio_channel_socket_get_remote_address(cioc, NULL);
    }
    g_assert(addr != NULL);
    uri = socket_uri(addr);
    qemu_set_info_str(&d->nc, "%s", uri);
    g_free(uri);
    qapi_event_send_netdev_stream_connected(d->nc.name, addr);
    qapi_free_SocketAddress(addr);
}

static void net_stream_server_listening(QIOTask *task, gpointer opaque)
{
    NetStreamData *d = opaque;
    QIOChannelSocket *listen_sioc = QIO_CHANNEL_SOCKET(d->listen_ioc);
    SocketAddress *addr;
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        qemu_set_info_str(&d->nc, "error: %s", error_get_pretty(err));
        error_free(err);
        return;
    }

    addr = qio_channel_socket_get_local_address(listen_sioc, NULL);
    g_assert(addr != NULL);
    if (!qemu_set_blocking(listen_sioc->fd, false, &err)) {
        qemu_set_info_str(&d->nc, "error: %s", error_get_pretty(err));
        error_free(err);
        return;
    }
    qapi_free_SocketAddress(addr);

    d->nc.link_down = true;
    d->listener = qio_net_listener_new();

    qemu_set_info_str(&d->nc, "listening");
    net_socket_rs_init(&d->rs, net_stream_data_rs_finalize, false);
    qio_net_listener_set_client_func(d->listener, d->listen, d,
                                     NULL);
    qio_net_listener_add(d->listener, listen_sioc);
}

static int net_stream_server_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  Error **errp)
{
    NetClientState *nc;
    NetStreamData *d;
    QIOChannelSocket *listen_sioc = qio_channel_socket_new();

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);
    d = DO_UPCAST(NetStreamData, nc, nc);
    d->send = net_stream_send;
    d->listen = net_stream_listen;
    qemu_set_info_str(&d->nc, "initializing");

    d->listen_ioc = QIO_CHANNEL(listen_sioc);
    qio_channel_socket_listen_async(listen_sioc, addr, 0,
                                    net_stream_server_listening, d,
                                    NULL, NULL);

    return 0;
}

static void net_stream_client_connected(QIOTask *task, gpointer opaque)
{
    NetStreamState *s = opaque;
    NetStreamData *d = &s->data;
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(d->ioc);
    SocketAddress *addr;
    gchar *uri;

    if (net_stream_data_client_connected(task, d) == -1) {
        net_stream_arm_reconnect(s);
        return;
    }

    addr = qio_channel_socket_get_remote_address(sioc, NULL);
    g_assert(addr != NULL);
    uri = socket_uri(addr);
    qemu_set_info_str(&d->nc, "%s", uri);
    g_free(uri);
    qapi_event_send_netdev_stream_connected(d->nc.name, addr);
    qapi_free_SocketAddress(addr);
}

static gboolean net_stream_reconnect(gpointer data)
{
    NetStreamState *s = data;
    QIOChannelSocket *sioc;

    s->timer_tag = 0;

    sioc = qio_channel_socket_new();
    s->data.ioc = QIO_CHANNEL(sioc);
    qio_channel_socket_connect_async(sioc, s->addr,
                                     net_stream_client_connected, s,
                                     NULL, NULL);
    return G_SOURCE_REMOVE;
}

static void net_stream_arm_reconnect(NetStreamState *s)
{
    if (s->reconnect_ms && s->timer_tag == 0) {
        qemu_set_info_str(&s->data.nc, "connecting");
        s->timer_tag = g_timeout_add(s->reconnect_ms, net_stream_reconnect, s);
    }
}

static int net_stream_client_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  uint32_t reconnect_ms,
                                  Error **errp)
{
    NetStreamState *s;
    NetClientState *nc;
    QIOChannelSocket *sioc = qio_channel_socket_new();

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);
    s = DO_UPCAST(NetStreamState, data.nc, nc);
    qemu_set_info_str(&s->data.nc, "connecting");

    s->data.ioc = QIO_CHANNEL(sioc);
    s->data.nc.link_down = true;
    s->data.send = net_stream_send;
    s->data.listen = net_stream_listen;

    s->reconnect_ms = reconnect_ms;
    if (reconnect_ms) {
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
                                      sock->has_reconnect_ms ?
                                          sock->reconnect_ms : 0,
                                      errp);
    }
    if (sock->has_reconnect_ms) {
        error_setg(errp, "'reconnect-ms' option is "
                         "incompatible with socket in server mode");
        return -1;
    }
    return net_stream_server_init(peer, "stream", name, sock->addr, errp);
}
