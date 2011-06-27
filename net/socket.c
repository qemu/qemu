/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "net/socket.h"

#include "config-host.h"

#include "net.h"
#include "qemu-char.h"
#include "qemu-common.h"
#include "qemu-error.h"
#include "qemu-option.h"
#include "qemu_socket.h"

typedef struct NetSocketState {
    VLANClientState nc;
    int fd;
    int state; /* 0 = getting length, 1 = getting data */
    unsigned int index;
    unsigned int packet_len;
    uint8_t buf[4096];
    struct sockaddr_in dgram_dst; /* contains inet host and port destination iff connectionless (SOCK_DGRAM) */
} NetSocketState;

typedef struct NetSocketListenState {
    VLANState *vlan;
    char *model;
    char *name;
    int fd;
} NetSocketListenState;

/* XXX: we consider we can send the whole packet without blocking */
static ssize_t net_socket_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    NetSocketState *s = DO_UPCAST(NetSocketState, nc, nc);
    uint32_t len;
    len = htonl(size);

    send_all(s->fd, (const uint8_t *)&len, sizeof(len));
    return send_all(s->fd, buf, size);
}

static ssize_t net_socket_receive_dgram(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    NetSocketState *s = DO_UPCAST(NetSocketState, nc, nc);

    return sendto(s->fd, (const void *)buf, size, 0,
                  (struct sockaddr *)&s->dgram_dst, sizeof(s->dgram_dst));
}

static void net_socket_send(void *opaque)
{
    NetSocketState *s = opaque;
    int size, err;
    unsigned l;
    uint8_t buf1[4096];
    const uint8_t *buf;

    size = recv(s->fd, (void *)buf1, sizeof(buf1), 0);
    if (size < 0) {
        err = socket_error();
        if (err != EWOULDBLOCK)
            goto eoc;
    } else if (size == 0) {
        /* end of connection */
    eoc:
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        return;
    }
    buf = buf1;
    while (size > 0) {
        /* reassemble a packet from the network */
        switch(s->state) {
        case 0:
            l = 4 - s->index;
            if (l > size)
                l = size;
            memcpy(s->buf + s->index, buf, l);
            buf += l;
            size -= l;
            s->index += l;
            if (s->index == 4) {
                /* got length */
                s->packet_len = ntohl(*(uint32_t *)s->buf);
                s->index = 0;
                s->state = 1;
            }
            break;
        case 1:
            l = s->packet_len - s->index;
            if (l > size)
                l = size;
            if (s->index + l <= sizeof(s->buf)) {
                memcpy(s->buf + s->index, buf, l);
            } else {
                fprintf(stderr, "serious error: oversized packet received,"
                    "connection terminated.\n");
                s->state = 0;
                goto eoc;
            }

            s->index += l;
            buf += l;
            size -= l;
            if (s->index >= s->packet_len) {
                qemu_send_packet(&s->nc, s->buf, s->packet_len);
                s->index = 0;
                s->state = 0;
            }
            break;
        }
    }
}

static void net_socket_send_dgram(void *opaque)
{
    NetSocketState *s = opaque;
    int size;

    size = recv(s->fd, (void *)s->buf, sizeof(s->buf), 0);
    if (size < 0)
        return;
    if (size == 0) {
        /* end of connection */
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        return;
    }
    qemu_send_packet(&s->nc, s->buf, size);
}

static int net_socket_mcast_create(struct sockaddr_in *mcastaddr, struct in_addr *localaddr)
{
    struct ip_mreq imr;
    int fd;
    int val, ret;
    if (!IN_MULTICAST(ntohl(mcastaddr->sin_addr.s_addr))) {
	fprintf(stderr, "qemu: error: specified mcastaddr \"%s\" (0x%08x) does not contain a multicast address\n",
		inet_ntoa(mcastaddr->sin_addr),
                (int)ntohl(mcastaddr->sin_addr.s_addr));
	return -1;

    }
    fd = qemu_socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket(PF_INET, SOCK_DGRAM)");
        return -1;
    }

    val = 1;
    ret=setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
	goto fail;
    }

    ret = bind(fd, (struct sockaddr *)mcastaddr, sizeof(*mcastaddr));
    if (ret < 0) {
        perror("bind");
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
                     (const char *)&imr, sizeof(struct ip_mreq));
    if (ret < 0) {
	perror("setsockopt(IP_ADD_MEMBERSHIP)");
	goto fail;
    }

    /* Force mcast msgs to loopback (eg. several QEMUs in same host */
    val = 1;
    ret=setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_IP, IP_MULTICAST_LOOP)");
	goto fail;
    }

    /* If a bind address is given, only send packets from that address */
    if (localaddr != NULL) {
        ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                         (const char *)localaddr, sizeof(*localaddr));
        if (ret < 0) {
            perror("setsockopt(IP_MULTICAST_IF)");
            goto fail;
        }
    }

    socket_set_nonblock(fd);
    return fd;
fail:
    if (fd >= 0)
        closesocket(fd);
    return -1;
}

static void net_socket_cleanup(VLANClientState *nc)
{
    NetSocketState *s = DO_UPCAST(NetSocketState, nc, nc);
    qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
    close(s->fd);
}

static NetClientInfo net_dgram_socket_info = {
    .type = NET_CLIENT_TYPE_SOCKET,
    .size = sizeof(NetSocketState),
    .receive = net_socket_receive_dgram,
    .cleanup = net_socket_cleanup,
};

static NetSocketState *net_socket_fd_init_dgram(VLANState *vlan,
                                                const char *model,
                                                const char *name,
                                                int fd, int is_connected)
{
    struct sockaddr_in saddr;
    int newfd;
    socklen_t saddr_len;
    VLANClientState *nc;
    NetSocketState *s;

    /* fd passed: multicast: "learn" dgram_dst address from bound address and save it
     * Because this may be "shared" socket from a "master" process, datagrams would be recv()
     * by ONLY ONE process: we must "clone" this dgram socket --jjo
     */

    if (is_connected) {
	if (getsockname(fd, (struct sockaddr *) &saddr, &saddr_len) == 0) {
	    /* must be bound */
	    if (saddr.sin_addr.s_addr==0) {
		fprintf(stderr, "qemu: error: init_dgram: fd=%d unbound, cannot setup multicast dst addr\n",
			fd);
		return NULL;
	    }
	    /* clone dgram socket */
	    newfd = net_socket_mcast_create(&saddr, NULL);
	    if (newfd < 0) {
		/* error already reported by net_socket_mcast_create() */
		close(fd);
		return NULL;
	    }
	    /* clone newfd to fd, close newfd */
	    dup2(newfd, fd);
	    close(newfd);

	} else {
	    fprintf(stderr, "qemu: error: init_dgram: fd=%d failed getsockname(): %s\n",
		    fd, strerror(errno));
	    return NULL;
	}
    }

    nc = qemu_new_net_client(&net_dgram_socket_info, vlan, NULL, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str),
	    "socket: fd=%d (%s mcast=%s:%d)",
	    fd, is_connected ? "cloned" : "",
	    inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));

    s = DO_UPCAST(NetSocketState, nc, nc);

    s->fd = fd;

    qemu_set_fd_handler(s->fd, net_socket_send_dgram, NULL, s);

    /* mcast: save bound address as dst */
    if (is_connected) s->dgram_dst=saddr;

    return s;
}

static void net_socket_connect(void *opaque)
{
    NetSocketState *s = opaque;
    qemu_set_fd_handler(s->fd, net_socket_send, NULL, s);
}

static NetClientInfo net_socket_info = {
    .type = NET_CLIENT_TYPE_SOCKET,
    .size = sizeof(NetSocketState),
    .receive = net_socket_receive,
    .cleanup = net_socket_cleanup,
};

static NetSocketState *net_socket_fd_init_stream(VLANState *vlan,
                                                 const char *model,
                                                 const char *name,
                                                 int fd, int is_connected)
{
    VLANClientState *nc;
    NetSocketState *s;

    nc = qemu_new_net_client(&net_socket_info, vlan, NULL, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "socket: fd=%d", fd);

    s = DO_UPCAST(NetSocketState, nc, nc);

    s->fd = fd;

    if (is_connected) {
        net_socket_connect(s);
    } else {
        qemu_set_fd_handler(s->fd, NULL, net_socket_connect, s);
    }
    return s;
}

static NetSocketState *net_socket_fd_init(VLANState *vlan,
                                          const char *model, const char *name,
                                          int fd, int is_connected)
{
    int so_type = -1, optlen=sizeof(so_type);

    if(getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&so_type,
        (socklen_t *)&optlen)< 0) {
	fprintf(stderr, "qemu: error: getsockopt(SO_TYPE) for fd=%d failed\n", fd);
	return NULL;
    }
    switch(so_type) {
    case SOCK_DGRAM:
        return net_socket_fd_init_dgram(vlan, model, name, fd, is_connected);
    case SOCK_STREAM:
        return net_socket_fd_init_stream(vlan, model, name, fd, is_connected);
    default:
        /* who knows ... this could be a eg. a pty, do warn and continue as stream */
        fprintf(stderr, "qemu: warning: socket type=%d for fd=%d is not SOCK_DGRAM or SOCK_STREAM\n", so_type, fd);
        return net_socket_fd_init_stream(vlan, model, name, fd, is_connected);
    }
    return NULL;
}

static void net_socket_accept(void *opaque)
{
    NetSocketListenState *s = opaque;
    NetSocketState *s1;
    struct sockaddr_in saddr;
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(saddr);
        fd = qemu_accept(s->fd, (struct sockaddr *)&saddr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            break;
        }
    }
    s1 = net_socket_fd_init(s->vlan, s->model, s->name, fd, 1);
    if (!s1) {
        closesocket(fd);
    } else {
        snprintf(s1->nc.info_str, sizeof(s1->nc.info_str),
                 "socket: connection from %s:%d",
                 inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    }
}

static int net_socket_listen_init(VLANState *vlan,
                                  const char *model,
                                  const char *name,
                                  const char *host_str)
{
    NetSocketListenState *s;
    int fd, val, ret;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    s = qemu_mallocz(sizeof(NetSocketListenState));

    fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret < 0) {
        perror("bind");
        return -1;
    }
    ret = listen(fd, 0);
    if (ret < 0) {
        perror("listen");
        return -1;
    }
    s->vlan = vlan;
    s->model = qemu_strdup(model);
    s->name = name ? qemu_strdup(name) : NULL;
    s->fd = fd;
    qemu_set_fd_handler(fd, net_socket_accept, NULL, s);
    return 0;
}

static int net_socket_connect_init(VLANState *vlan,
                                   const char *model,
                                   const char *name,
                                   const char *host_str)
{
    NetSocketState *s;
    int fd, connected, ret, err;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    connected = 0;
    for(;;) {
        ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
        if (ret < 0) {
            err = socket_error();
            if (err == EINTR || err == EWOULDBLOCK) {
            } else if (err == EINPROGRESS) {
                break;
#ifdef _WIN32
            } else if (err == WSAEALREADY || err == WSAEINVAL) {
                break;
#endif
            } else {
                perror("connect");
                closesocket(fd);
                return -1;
            }
        } else {
            connected = 1;
            break;
        }
    }
    s = net_socket_fd_init(vlan, model, name, fd, connected);
    if (!s)
        return -1;
    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "socket: connect to %s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;
}

static int net_socket_mcast_init(VLANState *vlan,
                                 const char *model,
                                 const char *name,
                                 const char *host_str,
                                 const char *localaddr_str)
{
    NetSocketState *s;
    int fd;
    struct sockaddr_in saddr;
    struct in_addr localaddr, *param_localaddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    if (localaddr_str != NULL) {
        if (inet_aton(localaddr_str, &localaddr) == 0)
            return -1;
        param_localaddr = &localaddr;
    } else {
        param_localaddr = NULL;
    }

    fd = net_socket_mcast_create(&saddr, param_localaddr);
    if (fd < 0)
	return -1;

    s = net_socket_fd_init(vlan, model, name, fd, 0);
    if (!s)
        return -1;

    s->dgram_dst = saddr;

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "socket: mcast=%s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;

}

int net_init_socket(QemuOpts *opts,
                    Monitor *mon,
                    const char *name,
                    VLANState *vlan)
{
    if (qemu_opt_get(opts, "fd")) {
        int fd;

        if (qemu_opt_get(opts, "listen") ||
            qemu_opt_get(opts, "connect") ||
            qemu_opt_get(opts, "mcast") ||
            qemu_opt_get(opts, "localaddr")) {
            error_report("listen=, connect=, mcast= and localaddr= is invalid with fd=");
            return -1;
        }

        fd = net_handle_fd_param(mon, qemu_opt_get(opts, "fd"));
        if (fd == -1) {
            return -1;
        }

        if (!net_socket_fd_init(vlan, "socket", name, fd, 1)) {
            close(fd);
            return -1;
        }
    } else if (qemu_opt_get(opts, "listen")) {
        const char *listen;

        if (qemu_opt_get(opts, "fd") ||
            qemu_opt_get(opts, "connect") ||
            qemu_opt_get(opts, "mcast") ||
            qemu_opt_get(opts, "localaddr")) {
            error_report("fd=, connect=, mcast= and localaddr= is invalid with listen=");
            return -1;
        }

        listen = qemu_opt_get(opts, "listen");

        if (net_socket_listen_init(vlan, "socket", name, listen) == -1) {
            return -1;
        }
    } else if (qemu_opt_get(opts, "connect")) {
        const char *connect;

        if (qemu_opt_get(opts, "fd") ||
            qemu_opt_get(opts, "listen") ||
            qemu_opt_get(opts, "mcast") ||
            qemu_opt_get(opts, "localaddr")) {
            error_report("fd=, listen=, mcast= and localaddr= is invalid with connect=");
            return -1;
        }

        connect = qemu_opt_get(opts, "connect");

        if (net_socket_connect_init(vlan, "socket", name, connect) == -1) {
            return -1;
        }
    } else if (qemu_opt_get(opts, "mcast")) {
        const char *mcast, *localaddr;

        if (qemu_opt_get(opts, "fd") ||
            qemu_opt_get(opts, "connect") ||
            qemu_opt_get(opts, "listen")) {
            error_report("fd=, connect= and listen= is invalid with mcast=");
            return -1;
        }

        mcast = qemu_opt_get(opts, "mcast");
        localaddr = qemu_opt_get(opts, "localaddr");

        if (net_socket_mcast_init(vlan, "socket", name, mcast, localaddr) == -1) {
            return -1;
        }
    } else {
        error_report("-socket requires fd=, listen=, connect= or mcast=");
        return -1;
    }

    return 0;
}
