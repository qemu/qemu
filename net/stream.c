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

typedef struct NetStreamState {
    NetClientState nc;
    int listen_fd;
    int fd;
    SocketReadState rs;
    unsigned int send_index;      /* number of bytes sent*/
    bool read_poll;               /* waiting to receive data? */
    bool write_poll;              /* waiting to transmit data? */
} NetStreamState;

static void net_stream_send(void *opaque);
static void net_stream_accept(void *opaque);
static void net_stream_writable(void *opaque);

static void net_stream_update_fd_handler(NetStreamState *s)
{
    qemu_set_fd_handler(s->fd,
                        s->read_poll ? net_stream_send : NULL,
                        s->write_poll ? net_stream_writable : NULL,
                        s);
}

static void net_stream_read_poll(NetStreamState *s, bool enable)
{
    s->read_poll = enable;
    net_stream_update_fd_handler(s);
}

static void net_stream_write_poll(NetStreamState *s, bool enable)
{
    s->write_poll = enable;
    net_stream_update_fd_handler(s);
}

static void net_stream_writable(void *opaque)
{
    NetStreamState *s = opaque;

    net_stream_write_poll(s, false);

    qemu_flush_queued_packets(&s->nc);
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
    size_t remaining;
    ssize_t ret;

    remaining = iov_size(iov, 2) - s->send_index;
    ret = iov_send(s->fd, iov, 2, s->send_index, remaining);

    if (ret == -1 && errno == EAGAIN) {
        ret = 0; /* handled further down */
    }
    if (ret == -1) {
        s->send_index = 0;
        return -errno;
    }
    if (ret < (ssize_t)remaining) {
        s->send_index += ret;
        net_stream_write_poll(s, true);
        return 0;
    }
    s->send_index = 0;
    return size;
}

static void net_stream_send_completed(NetClientState *nc, ssize_t len)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, nc, nc);

    if (!s->read_poll) {
        net_stream_read_poll(s, true);
    }
}

static void net_stream_rs_finalize(SocketReadState *rs)
{
    NetStreamState *s = container_of(rs, NetStreamState, rs);

    if (qemu_send_packet_async(&s->nc, rs->buf,
                               rs->packet_len,
                               net_stream_send_completed) == 0) {
        net_stream_read_poll(s, false);
    }
}

static void net_stream_send(void *opaque)
{
    NetStreamState *s = opaque;
    int size;
    int ret;
    uint8_t buf1[NET_BUFSIZE];
    const uint8_t *buf;

    size = recv(s->fd, buf1, sizeof(buf1), 0);
    if (size < 0) {
        if (errno != EWOULDBLOCK) {
            goto eoc;
        }
    } else if (size == 0) {
        /* end of connection */
    eoc:
        net_stream_read_poll(s, false);
        net_stream_write_poll(s, false);
        if (s->listen_fd != -1) {
            qemu_set_fd_handler(s->listen_fd, net_stream_accept, NULL, s);
        }
        closesocket(s->fd);

        s->fd = -1;
        net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);
        s->nc.link_down = true;
        qemu_set_info_str(&s->nc, "");

        return;
    }
    buf = buf1;

    ret = net_fill_rstate(&s->rs, buf, size);

    if (ret == -1) {
        goto eoc;
    }
}

static void net_stream_cleanup(NetClientState *nc)
{
    NetStreamState *s = DO_UPCAST(NetStreamState, nc, nc);
    if (s->fd != -1) {
        net_stream_read_poll(s, false);
        net_stream_write_poll(s, false);
        close(s->fd);
        s->fd = -1;
    }
    if (s->listen_fd != -1) {
        qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
        closesocket(s->listen_fd);
        s->listen_fd = -1;
    }
}

static void net_stream_connect(void *opaque)
{
    NetStreamState *s = opaque;
    net_stream_read_poll(s, true);
}

static NetClientInfo net_stream_info = {
    .type = NET_CLIENT_DRIVER_STREAM,
    .size = sizeof(NetStreamState),
    .receive = net_stream_receive,
    .cleanup = net_stream_cleanup,
};

static NetStreamState *net_stream_fd_init(NetClientState *peer,
                                          const char *model,
                                          const char *name,
                                          int fd, int is_connected)
{
    NetClientState *nc;
    NetStreamState *s;

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);

    qemu_set_info_str(nc, "fd=%d", fd);

    s = DO_UPCAST(NetStreamState, nc, nc);

    s->fd = fd;
    s->listen_fd = -1;
    net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);

    /* Disable Nagle algorithm on TCP sockets to reduce latency */
    socket_set_nodelay(fd);

    if (is_connected) {
        net_stream_connect(s);
    } else {
        qemu_set_fd_handler(s->fd, NULL, net_stream_connect, s);
    }
    return s;
}

static void net_stream_accept(void *opaque)
{
    NetStreamState *s = opaque;
    struct sockaddr_in saddr;
    socklen_t len;
    int fd;

    for (;;) {
        len = sizeof(saddr);
        fd = qemu_accept(s->listen_fd, (struct sockaddr *)&saddr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
            break;
        }
    }

    s->fd = fd;
    s->nc.link_down = false;
    net_stream_connect(s);
    qemu_set_info_str(&s->nc, "connection from %s:%d",
                      inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
}

static int net_stream_server_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  Error **errp)
{
    NetClientState *nc;
    NetStreamState *s;
    int fd, ret;

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET: {
        struct sockaddr_in saddr_in;

        if (convert_host_port(&saddr_in, addr->u.inet.host, addr->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        socket_set_fast_reuse(fd);

        ret = bind(fd, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                             inet_ntoa(saddr_in.sin_addr));
            closesocket(fd);
            return -1;
        }
        break;
    }
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), addr->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        break;
    default:
        error_setg(errp, "only support inet or fd type");
        return -1;
    }

    ret = listen(fd, 0);
    if (ret < 0) {
        error_setg_errno(errp, errno, "can't listen on socket");
        closesocket(fd);
        return -1;
    }

    nc = qemu_new_net_client(&net_stream_info, peer, model, name);
    s = DO_UPCAST(NetStreamState, nc, nc);
    s->fd = -1;
    s->listen_fd = fd;
    s->nc.link_down = true;
    net_socket_rs_init(&s->rs, net_stream_rs_finalize, false);

    qemu_set_fd_handler(s->listen_fd, net_stream_accept, NULL, s);
    return 0;
}

static int net_stream_client_init(NetClientState *peer,
                                  const char *model,
                                  const char *name,
                                  SocketAddress *addr,
                                  Error **errp)
{
    NetStreamState *s;
    struct sockaddr_in saddr_in;
    int fd, connected, ret;

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        if (convert_host_port(&saddr_in, addr->u.inet.host, addr->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        connected = 0;
        for (;;) {
            ret = connect(fd, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    /* continue */
                } else if (errno == EINPROGRESS ||
                           errno == EALREADY) {
                    break;
                } else {
                    error_setg_errno(errp, errno, "can't connect socket");
                    closesocket(fd);
                    return -1;
                }
            } else {
                connected = 1;
                break;
            }
        }
        break;
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), addr->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        connected = 1;
        break;
    default:
        error_setg(errp, "only support inet or fd type");
        return -1;
    }

    s = net_stream_fd_init(peer, model, name, fd, connected);

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        qemu_set_info_str(&s->nc, "connect to %s:%d",
                          inet_ntoa(saddr_in.sin_addr),
                          ntohs(saddr_in.sin_port));
        break;
    case SOCKET_ADDRESS_TYPE_FD:
        qemu_set_info_str(&s->nc, "connect to fd %d", fd);
        break;
    default:
        g_assert_not_reached();
    }

    return 0;
}

int net_init_stream(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp)
{
    const NetdevStreamOptions *sock;

    assert(netdev->type == NET_CLIENT_DRIVER_STREAM);
    sock = &netdev->u.stream;

    if (!sock->has_server || !sock->server) {
        return net_stream_client_init(peer, "stream", name, sock->addr, errp);
    }
    return net_stream_server_init(peer, "stream", name, sock->addr, errp);
}
