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

typedef struct NetDgramState {
    NetClientState nc;
    int fd;
    SocketReadState rs;
    bool read_poll;               /* waiting to receive data? */
    bool write_poll;              /* waiting to transmit data? */
    /* contains destination iff connectionless */
    struct sockaddr *dest_addr;
    socklen_t dest_len;
} NetDgramState;

static void net_dgram_send(void *opaque);
static void net_dgram_writable(void *opaque);

static void net_dgram_update_fd_handler(NetDgramState *s)
{
    qemu_set_fd_handler(s->fd,
                        s->read_poll ? net_dgram_send : NULL,
                        s->write_poll ? net_dgram_writable : NULL,
                        s);
}

static void net_dgram_read_poll(NetDgramState *s, bool enable)
{
    s->read_poll = enable;
    net_dgram_update_fd_handler(s);
}

static void net_dgram_write_poll(NetDgramState *s, bool enable)
{
    s->write_poll = enable;
    net_dgram_update_fd_handler(s);
}

static void net_dgram_writable(void *opaque)
{
    NetDgramState *s = opaque;

    net_dgram_write_poll(s, false);

    qemu_flush_queued_packets(&s->nc);
}

static ssize_t net_dgram_receive(NetClientState *nc,
                                 const uint8_t *buf, size_t size)
{
    NetDgramState *s = DO_UPCAST(NetDgramState, nc, nc);
    ssize_t ret;

    do {
        if (s->dest_addr) {
            ret = sendto(s->fd, buf, size, 0, s->dest_addr, s->dest_len);
        } else {
            ret = send(s->fd, buf, size, 0);
        }
    } while (ret == -1 && errno == EINTR);

    if (ret == -1 && errno == EAGAIN) {
        net_dgram_write_poll(s, true);
        return 0;
    }
    return ret;
}

static void net_dgram_send_completed(NetClientState *nc, ssize_t len)
{
    NetDgramState *s = DO_UPCAST(NetDgramState, nc, nc);

    if (!s->read_poll) {
        net_dgram_read_poll(s, true);
    }
}

static void net_dgram_rs_finalize(SocketReadState *rs)
{
    NetDgramState *s = container_of(rs, NetDgramState, rs);

    if (qemu_send_packet_async(&s->nc, rs->buf,
                               rs->packet_len,
                               net_dgram_send_completed) == 0) {
        net_dgram_read_poll(s, false);
    }
}

static void net_dgram_send(void *opaque)
{
    NetDgramState *s = opaque;
    int size;

    size = recv(s->fd, s->rs.buf, sizeof(s->rs.buf), 0);
    if (size < 0) {
        return;
    }
    if (size == 0) {
        /* end of connection */
        net_dgram_read_poll(s, false);
        net_dgram_write_poll(s, false);
        return;
    }
    if (qemu_send_packet_async(&s->nc, s->rs.buf, size,
                               net_dgram_send_completed) == 0) {
        net_dgram_read_poll(s, false);
    }
}

static int net_dgram_mcast_create(struct sockaddr_in *mcastaddr,
                                  struct in_addr *localaddr,
                                  Error **errp)
{
    struct ip_mreq imr;
    int fd;
    int val, ret;
#ifdef __OpenBSD__
    unsigned char loop;
#else
    int loop;
#endif

    if (!IN_MULTICAST(ntohl(mcastaddr->sin_addr.s_addr))) {
        error_setg(errp, "specified mcastaddr %s (0x%08x) "
                   "does not contain a multicast address",
                   inet_ntoa(mcastaddr->sin_addr),
                   (int)ntohl(mcastaddr->sin_addr.s_addr));
        return -1;
    }

    fd = qemu_socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "can't create datagram socket");
        return -1;
    }

    /*
     * Allow multiple sockets to bind the same multicast ip and port by setting
     * SO_REUSEADDR. This is the only situation where SO_REUSEADDR should be set
     * on windows. Use socket_set_fast_reuse otherwise as it sets SO_REUSEADDR
     * only on posix systems.
     */
    val = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (ret < 0) {
        error_setg_errno(errp, errno, "can't set socket option SO_REUSEADDR");
        goto fail;
    }

    ret = bind(fd, (struct sockaddr *)mcastaddr, sizeof(*mcastaddr));
    if (ret < 0) {
        error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                         inet_ntoa(mcastaddr->sin_addr));
        goto fail;
    }

    /* Add host to multicast group */
    imr.imr_multiaddr = mcastaddr->sin_addr;
    if (localaddr) {
        imr.imr_interface = *localaddr;
    } else {
        imr.imr_interface.s_addr = htonl(INADDR_ANY);
    }

    ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &imr, sizeof(struct ip_mreq));
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "can't add socket to multicast group %s",
                         inet_ntoa(imr.imr_multiaddr));
        goto fail;
    }

    /* Force mcast msgs to loopback (eg. several QEMUs in same host */
    loop = 1;
    ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                     &loop, sizeof(loop));
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "can't force multicast message to loopback");
        goto fail;
    }

    /* If a bind address is given, only send packets from that address */
    if (localaddr != NULL) {
        ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                         localaddr, sizeof(*localaddr));
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "can't set the default network send interface");
            goto fail;
        }
    }

    qemu_socket_set_nonblock(fd);
    return fd;
fail:
    if (fd >= 0) {
        close(fd);
    }
    return -1;
}

static void net_dgram_cleanup(NetClientState *nc)
{
    NetDgramState *s = DO_UPCAST(NetDgramState, nc, nc);
    if (s->fd != -1) {
        net_dgram_read_poll(s, false);
        net_dgram_write_poll(s, false);
        close(s->fd);
        s->fd = -1;
    }
    g_free(s->dest_addr);
    s->dest_addr = NULL;
    s->dest_len = 0;
}

static NetClientInfo net_dgram_socket_info = {
    .type = NET_CLIENT_DRIVER_DGRAM,
    .size = sizeof(NetDgramState),
    .receive = net_dgram_receive,
    .cleanup = net_dgram_cleanup,
};

static NetDgramState *net_dgram_fd_init(NetClientState *peer,
                                        const char *model,
                                        const char *name,
                                        int fd,
                                        Error **errp)
{
    NetClientState *nc;
    NetDgramState *s;

    nc = qemu_new_net_client(&net_dgram_socket_info, peer, model, name);

    s = DO_UPCAST(NetDgramState, nc, nc);

    s->fd = fd;
    net_socket_rs_init(&s->rs, net_dgram_rs_finalize, false);
    net_dgram_read_poll(s, true);

    return s;
}

static int net_dgram_mcast_init(NetClientState *peer,
                                const char *model,
                                const char *name,
                                SocketAddress *remote,
                                SocketAddress *local,
                                Error **errp)
{
    NetDgramState *s;
    int fd, ret;
    struct sockaddr_in *saddr;

    if (remote->type != SOCKET_ADDRESS_TYPE_INET) {
        error_setg(errp, "multicast only support inet type");
        return -1;
    }

    saddr = g_new(struct sockaddr_in, 1);
    if (convert_host_port(saddr, remote->u.inet.host, remote->u.inet.port,
                          errp) < 0) {
        g_free(saddr);
        return -1;
    }

    if (!local) {
        fd = net_dgram_mcast_create(saddr, NULL, errp);
        if (fd < 0) {
            g_free(saddr);
            return -1;
        }
    } else {
        switch (local->type) {
        case SOCKET_ADDRESS_TYPE_INET: {
            struct in_addr localaddr;

            if (inet_aton(local->u.inet.host, &localaddr) == 0) {
                g_free(saddr);
                error_setg(errp, "localaddr '%s' is not a valid IPv4 address",
                           local->u.inet.host);
                return -1;
            }

            fd = net_dgram_mcast_create(saddr, &localaddr, errp);
            if (fd < 0) {
                g_free(saddr);
                return -1;
            }
            break;
        }
        case SOCKET_ADDRESS_TYPE_FD: {
            int newfd;

            fd = monitor_fd_param(monitor_cur(), local->u.fd.str, errp);
            if (fd == -1) {
                g_free(saddr);
                return -1;
            }
            ret = qemu_socket_try_set_nonblock(fd);
            if (ret < 0) {
                g_free(saddr);
                error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                                 name, fd);
                return -1;
            }

            /*
             * fd passed: multicast: "learn" dest_addr address from bound
             * address and save it. Because this may be "shared" socket from a
             * "master" process, datagrams would be recv() by ONLY ONE process:
             * we must "clone" this dgram socket --jjo
             */

            saddr = g_new(struct sockaddr_in, 1);

            if (convert_host_port(saddr, local->u.inet.host, local->u.inet.port,
                                  errp) < 0) {
                g_free(saddr);
                close(fd);
                return -1;
            }

            /* must be bound */
            if (saddr->sin_addr.s_addr == 0) {
                error_setg(errp, "can't setup multicast destination address");
                g_free(saddr);
                close(fd);
                return -1;
            }
            /* clone dgram socket */
            newfd = net_dgram_mcast_create(saddr, NULL, errp);
            if (newfd < 0) {
                g_free(saddr);
                close(fd);
                return -1;
            }
            /* clone newfd to fd, close newfd */
            dup2(newfd, fd);
            close(newfd);
            break;
        }
        default:
            g_free(saddr);
            error_setg(errp, "only support inet or fd type for local");
            return -1;
        }
    }

    s = net_dgram_fd_init(peer, model, name, fd, errp);
    if (!s) {
        g_free(saddr);
        return -1;
    }

    g_assert(s->dest_addr == NULL);
    s->dest_addr = (struct sockaddr *)saddr;
    s->dest_len = sizeof(*saddr);

    if (!local) {
        qemu_set_info_str(&s->nc, "mcast=%s:%d",
                          inet_ntoa(saddr->sin_addr),
                          ntohs(saddr->sin_port));
    } else {
        switch (local->type) {
        case SOCKET_ADDRESS_TYPE_INET:
            qemu_set_info_str(&s->nc, "mcast=%s:%d",
                              inet_ntoa(saddr->sin_addr),
                              ntohs(saddr->sin_port));
            break;
        case SOCKET_ADDRESS_TYPE_FD:
            qemu_set_info_str(&s->nc, "fd=%d (cloned mcast=%s:%d)",
                              fd, inet_ntoa(saddr->sin_addr),
                              ntohs(saddr->sin_port));
            break;
        default:
            g_assert_not_reached();
        }
    }

    return 0;

}


int net_init_dgram(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp)
{
    NetDgramState *s;
    int fd, ret;
    SocketAddress *remote, *local;
    struct sockaddr *dest_addr;
    struct sockaddr_in laddr_in, raddr_in;
    struct sockaddr_un laddr_un, raddr_un;
    socklen_t dest_len;

    assert(netdev->type == NET_CLIENT_DRIVER_DGRAM);

    remote = netdev->u.dgram.remote;
    local = netdev->u.dgram.local;

    /* detect multicast address */
    if (remote && remote->type == SOCKET_ADDRESS_TYPE_INET) {
        struct sockaddr_in mcastaddr;

        if (convert_host_port(&mcastaddr, remote->u.inet.host,
                              remote->u.inet.port, errp) < 0) {
            return -1;
        }

        if (IN_MULTICAST(ntohl(mcastaddr.sin_addr.s_addr))) {
            return net_dgram_mcast_init(peer, "dram", name, remote, local,
                                           errp);
        }
    }

    /* unicast address */
    if (!local) {
        error_setg(errp, "dgram requires local= parameter");
        return -1;
    }

    if (remote) {
        if (local->type == SOCKET_ADDRESS_TYPE_FD) {
            error_setg(errp, "don't set remote with local.fd");
            return -1;
        }
        if (remote->type != local->type) {
            error_setg(errp, "remote and local types must be the same");
            return -1;
        }
    } else {
        if (local->type != SOCKET_ADDRESS_TYPE_FD) {
            error_setg(errp,
                       "type=inet or type=unix requires remote parameter");
            return -1;
        }
    }

    switch (local->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        if (convert_host_port(&laddr_in, local->u.inet.host, local->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        if (convert_host_port(&raddr_in, remote->u.inet.host,
                              remote->u.inet.port, errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create datagram socket");
            return -1;
        }

        ret = socket_set_fast_reuse(fd);
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "can't set socket option SO_REUSEADDR");
            close(fd);
            return -1;
        }
        ret = bind(fd, (struct sockaddr *)&laddr_in, sizeof(laddr_in));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                             inet_ntoa(laddr_in.sin_addr));
            close(fd);
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        dest_len = sizeof(raddr_in);
        dest_addr = g_malloc(dest_len);
        memcpy(dest_addr, &raddr_in, dest_len);
        break;
    case SOCKET_ADDRESS_TYPE_UNIX:
        ret = unlink(local->u.q_unix.path);
        if (ret < 0 && errno != ENOENT) {
            error_setg_errno(errp, errno, "failed to unlink socket %s",
                             local->u.q_unix.path);
            return -1;
        }

        laddr_un.sun_family = PF_UNIX;
        ret = snprintf(laddr_un.sun_path, sizeof(laddr_un.sun_path), "%s",
                       local->u.q_unix.path);
        if (ret < 0 || ret >= sizeof(laddr_un.sun_path)) {
            error_setg(errp, "UNIX socket path '%s' is too long",
                       local->u.q_unix.path);
            error_append_hint(errp, "Path must be less than %zu bytes\n",
                              sizeof(laddr_un.sun_path));
        }

        raddr_un.sun_family = PF_UNIX;
        ret = snprintf(raddr_un.sun_path, sizeof(raddr_un.sun_path), "%s",
                       remote->u.q_unix.path);
        if (ret < 0 || ret >= sizeof(raddr_un.sun_path)) {
            error_setg(errp, "UNIX socket path '%s' is too long",
                       remote->u.q_unix.path);
            error_append_hint(errp, "Path must be less than %zu bytes\n",
                              sizeof(raddr_un.sun_path));
        }

        fd = qemu_socket(PF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create datagram socket");
            return -1;
        }

        ret = bind(fd, (struct sockaddr *)&laddr_un, sizeof(laddr_un));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't bind unix=%s to socket",
                             laddr_un.sun_path);
            close(fd);
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        dest_len = sizeof(raddr_un);
        dest_addr = g_malloc(dest_len);
        memcpy(dest_addr, &raddr_un, dest_len);
        break;
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), local->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        dest_addr = NULL;
        dest_len = 0;
        break;
    default:
        error_setg(errp, "only support inet or fd type for local");
        return -1;
    }

    s = net_dgram_fd_init(peer, "dgram", name, fd, errp);
    if (!s) {
        return -1;
    }

    if (remote) {
        g_assert(s->dest_addr == NULL);
        s->dest_addr = dest_addr;
        s->dest_len = dest_len;
    }

    switch (local->type) {
    case SOCKET_ADDRESS_TYPE_INET:
        qemu_set_info_str(&s->nc, "udp=%s:%d/%s:%d",
                          inet_ntoa(laddr_in.sin_addr),
                          ntohs(laddr_in.sin_port),
                          inet_ntoa(raddr_in.sin_addr),
                          ntohs(raddr_in.sin_port));
        break;
    case SOCKET_ADDRESS_TYPE_UNIX:
        qemu_set_info_str(&s->nc, "udp=%s:%s",
                          laddr_un.sun_path, raddr_un.sun_path);
        break;
    case SOCKET_ADDRESS_TYPE_FD: {
        SocketAddress *sa;
        SocketAddressType sa_type;

        sa = socket_local_address(fd, errp);
        if (sa) {
            sa_type = sa->type;
            qapi_free_SocketAddress(sa);

            qemu_set_info_str(&s->nc, "fd=%d %s", fd,
                              SocketAddressType_str(sa_type));
        } else {
            qemu_set_info_str(&s->nc, "fd=%d", fd);
        }
        break;
    }
    default:
        g_assert_not_reached();
    }

    return 0;
}
