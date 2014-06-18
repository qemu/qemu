/*
 *  inet and unix socket functions for qemu
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "monitor/monitor.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif

/* used temporarily until all users are converted to QemuOpts */
QemuOptsList socket_optslist = {
    .name = "socket",
    .head = QTAILQ_HEAD_INITIALIZER(socket_optslist.head),
    .desc = {
        {
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },
        { /* end if list */ }
    },
};

static int inet_getport(struct addrinfo *e)
{
    struct sockaddr_in *i4;
    struct sockaddr_in6 *i6;

    switch (e->ai_family) {
    case PF_INET6:
        i6 = (void*)e->ai_addr;
        return ntohs(i6->sin6_port);
    case PF_INET:
        i4 = (void*)e->ai_addr;
        return ntohs(i4->sin_port);
    default:
        return 0;
    }
}

static void inet_setport(struct addrinfo *e, int port)
{
    struct sockaddr_in *i4;
    struct sockaddr_in6 *i6;

    switch (e->ai_family) {
    case PF_INET6:
        i6 = (void*)e->ai_addr;
        i6->sin6_port = htons(port);
        break;
    case PF_INET:
        i4 = (void*)e->ai_addr;
        i4->sin_port = htons(port);
        break;
    }
}

const char *inet_strfamily(int family)
{
    switch (family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
    case PF_UNIX:  return "unix";
    }
    return "unknown";
}

NetworkAddressFamily inet_netfamily(int family)
{
    switch (family) {
    case PF_INET6: return NETWORK_ADDRESS_FAMILY_IPV6;
    case PF_INET:  return NETWORK_ADDRESS_FAMILY_IPV4;
    case PF_UNIX:  return NETWORK_ADDRESS_FAMILY_UNIX;
    }
    return NETWORK_ADDRESS_FAMILY_UNKNOWN;
}

int inet_listen_opts(QemuOpts *opts, int port_offset, Error **errp)
{
    struct addrinfo ai,*res,*e;
    const char *addr;
    char port[33];
    char uaddr[INET6_ADDRSTRLEN+1];
    char uport[33];
    int slisten, rc, to, port_min, port_max, p;

    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    if ((qemu_opt_get(opts, "host") == NULL) ||
        (qemu_opt_get(opts, "port") == NULL)) {
        error_setg(errp, "host and/or port not specified");
        return -1;
    }
    pstrcpy(port, sizeof(port), qemu_opt_get(opts, "port"));
    addr = qemu_opt_get(opts, "host");

    to = qemu_opt_get_number(opts, "to", 0);
    if (qemu_opt_get_bool(opts, "ipv4", 0))
        ai.ai_family = PF_INET;
    if (qemu_opt_get_bool(opts, "ipv6", 0))
        ai.ai_family = PF_INET6;

    /* lookup */
    if (port_offset) {
        unsigned long long baseport;
        if (parse_uint_full(port, &baseport, 10) < 0) {
            error_setg(errp, "can't convert to a number: %s", port);
            return -1;
        }
        if (baseport > 65535 ||
            baseport + port_offset > 65535) {
            error_setg(errp, "port %s out of range", port);
            return -1;
        }
        snprintf(port, sizeof(port), "%d", (int)baseport + port_offset);
    }
    rc = getaddrinfo(strlen(addr) ? addr : NULL, port, &ai, &res);
    if (rc != 0) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
        return -1;
    }

    /* create socket + bind */
    for (e = res; e != NULL; e = e->ai_next) {
        getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
		        uaddr,INET6_ADDRSTRLEN,uport,32,
		        NI_NUMERICHOST | NI_NUMERICSERV);
        slisten = qemu_socket(e->ai_family, e->ai_socktype, e->ai_protocol);
        if (slisten < 0) {
            if (!e->ai_next) {
                error_set_errno(errp, errno, QERR_SOCKET_CREATE_FAILED);
            }
            continue;
        }

        socket_set_fast_reuse(slisten);
#ifdef IPV6_V6ONLY
        if (e->ai_family == PF_INET6) {
            /* listen on both ipv4 and ipv6 */
            const int off = 0;
            qemu_setsockopt(slisten, IPPROTO_IPV6, IPV6_V6ONLY, &off,
                            sizeof(off));
        }
#endif

        port_min = inet_getport(e);
        port_max = to ? to + port_offset : port_min;
        for (p = port_min; p <= port_max; p++) {
            inet_setport(e, p);
            if (bind(slisten, e->ai_addr, e->ai_addrlen) == 0) {
                goto listen;
            }
            if (p == port_max) {
                if (!e->ai_next) {
                    error_set_errno(errp, errno, QERR_SOCKET_BIND_FAILED);
                }
            }
        }
        closesocket(slisten);
    }
    freeaddrinfo(res);
    return -1;

listen:
    if (listen(slisten,1) != 0) {
        error_set_errno(errp, errno, QERR_SOCKET_LISTEN_FAILED);
        closesocket(slisten);
        freeaddrinfo(res);
        return -1;
    }
    snprintf(uport, sizeof(uport), "%d", inet_getport(e) - port_offset);
    qemu_opt_set(opts, "host", uaddr);
    qemu_opt_set(opts, "port", uport);
    qemu_opt_set(opts, "ipv6", (e->ai_family == PF_INET6) ? "on" : "off");
    qemu_opt_set(opts, "ipv4", (e->ai_family != PF_INET6) ? "on" : "off");
    freeaddrinfo(res);
    return slisten;
}

#ifdef _WIN32
#define QEMU_SOCKET_RC_INPROGRESS(rc) \
    ((rc) == -EINPROGRESS || (rc) == -EWOULDBLOCK || (rc) == -WSAEALREADY)
#else
#define QEMU_SOCKET_RC_INPROGRESS(rc) \
    ((rc) == -EINPROGRESS)
#endif

/* Struct to store connect state for non blocking connect */
typedef struct ConnectState {
    int fd;
    struct addrinfo *addr_list;
    struct addrinfo *current_addr;
    NonBlockingConnectHandler *callback;
    void *opaque;
} ConnectState;

static int inet_connect_addr(struct addrinfo *addr, bool *in_progress,
                             ConnectState *connect_state, Error **errp);

static void wait_for_connect(void *opaque)
{
    ConnectState *s = opaque;
    int val = 0, rc = 0;
    socklen_t valsize = sizeof(val);
    bool in_progress;

    qemu_set_fd_handler2(s->fd, NULL, NULL, NULL, NULL);

    do {
        rc = qemu_getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &val, &valsize);
    } while (rc == -1 && socket_error() == EINTR);

    /* update rc to contain error */
    if (!rc && val) {
        rc = -1;
    }

    /* connect error */
    if (rc < 0) {
        closesocket(s->fd);
        s->fd = rc;
    }

    /* try to connect to the next address on the list */
    if (s->current_addr) {
        while (s->current_addr->ai_next != NULL && s->fd < 0) {
            s->current_addr = s->current_addr->ai_next;
            s->fd = inet_connect_addr(s->current_addr, &in_progress, s, NULL);
            /* connect in progress */
            if (in_progress) {
                return;
            }
        }

        freeaddrinfo(s->addr_list);
    }

    if (s->callback) {
        s->callback(s->fd, s->opaque);
    }
    g_free(s);
}

static int inet_connect_addr(struct addrinfo *addr, bool *in_progress,
                             ConnectState *connect_state, Error **errp)
{
    int sock, rc;

    *in_progress = false;

    sock = qemu_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CREATE_FAILED);
        return -1;
    }
    socket_set_fast_reuse(sock);
    if (connect_state != NULL) {
        qemu_set_nonblock(sock);
    }
    /* connect to peer */
    do {
        rc = 0;
        if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
            rc = -socket_error();
        }
    } while (rc == -EINTR);

    if (connect_state != NULL && QEMU_SOCKET_RC_INPROGRESS(rc)) {
        connect_state->fd = sock;
        qemu_set_fd_handler2(sock, NULL, NULL, wait_for_connect,
                             connect_state);
        *in_progress = true;
    } else if (rc < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CONNECT_FAILED);
        closesocket(sock);
        return -1;
    }
    return sock;
}

static struct addrinfo *inet_parse_connect_opts(QemuOpts *opts, Error **errp)
{
    struct addrinfo ai, *res;
    int rc;
    const char *addr;
    const char *port;

    memset(&ai, 0, sizeof(ai));

    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    addr = qemu_opt_get(opts, "host");
    port = qemu_opt_get(opts, "port");
    if (addr == NULL || port == NULL) {
        error_setg(errp, "host and/or port not specified");
        return NULL;
    }

    if (qemu_opt_get_bool(opts, "ipv4", 0)) {
        ai.ai_family = PF_INET;
    }
    if (qemu_opt_get_bool(opts, "ipv6", 0)) {
        ai.ai_family = PF_INET6;
    }

    /* lookup */
    rc = getaddrinfo(addr, port, &ai, &res);
    if (rc != 0) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
        return NULL;
    }
    return res;
}

/**
 * Create a socket and connect it to an address.
 *
 * @opts: QEMU options, recognized parameters strings "host" and "port",
 *        bools "ipv4" and "ipv6".
 * @errp: set on error
 * @callback: callback function for non-blocking connect
 * @opaque: opaque for callback function
 *
 * Returns: -1 on error, file descriptor on success.
 *
 * If @callback is non-null, the connect is non-blocking.  If this
 * function succeeds, callback will be called when the connection
 * completes, with the file descriptor on success, or -1 on error.
 */
int inet_connect_opts(QemuOpts *opts, Error **errp,
                      NonBlockingConnectHandler *callback, void *opaque)
{
    Error *local_err = NULL;
    struct addrinfo *res, *e;
    int sock = -1;
    bool in_progress;
    ConnectState *connect_state = NULL;

    res = inet_parse_connect_opts(opts, errp);
    if (!res) {
        return -1;
    }

    if (callback != NULL) {
        connect_state = g_malloc0(sizeof(*connect_state));
        connect_state->addr_list = res;
        connect_state->callback = callback;
        connect_state->opaque = opaque;
    }

    for (e = res; e != NULL; e = e->ai_next) {
        error_free(local_err);
        local_err = NULL;
        if (connect_state != NULL) {
            connect_state->current_addr = e;
        }
        sock = inet_connect_addr(e, &in_progress, connect_state, &local_err);
        if (sock >= 0) {
            break;
        }
    }

    if (sock < 0) {
        error_propagate(errp, local_err);
    } else if (in_progress) {
        /* wait_for_connect() will do the rest */
        return sock;
    } else {
        if (callback) {
            callback(sock, opaque);
        }
    }
    g_free(connect_state);
    freeaddrinfo(res);
    return sock;
}

int inet_dgram_opts(QemuOpts *opts, Error **errp)
{
    struct addrinfo ai, *peer = NULL, *local = NULL;
    const char *addr;
    const char *port;
    int sock = -1, rc;

    /* lookup peer addr */
    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = SOCK_DGRAM;

    addr = qemu_opt_get(opts, "host");
    port = qemu_opt_get(opts, "port");
    if (addr == NULL || strlen(addr) == 0) {
        addr = "localhost";
    }
    if (port == NULL || strlen(port) == 0) {
        error_setg(errp, "remote port not specified");
        return -1;
    }

    if (qemu_opt_get_bool(opts, "ipv4", 0))
        ai.ai_family = PF_INET;
    if (qemu_opt_get_bool(opts, "ipv6", 0))
        ai.ai_family = PF_INET6;

    if (0 != (rc = getaddrinfo(addr, port, &ai, &peer))) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
	return -1;
    }

    /* lookup local addr */
    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE;
    ai.ai_family = peer->ai_family;
    ai.ai_socktype = SOCK_DGRAM;

    addr = qemu_opt_get(opts, "localaddr");
    port = qemu_opt_get(opts, "localport");
    if (addr == NULL || strlen(addr) == 0) {
        addr = NULL;
    }
    if (!port || strlen(port) == 0)
        port = "0";

    if (0 != (rc = getaddrinfo(addr, port, &ai, &local))) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
        goto err;
    }

    /* create socket */
    sock = qemu_socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    if (sock < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CREATE_FAILED);
        goto err;
    }
    socket_set_fast_reuse(sock);

    /* bind socket */
    if (bind(sock, local->ai_addr, local->ai_addrlen) < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_BIND_FAILED);
        goto err;
    }

    /* connect to peer */
    if (connect(sock,peer->ai_addr,peer->ai_addrlen) < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CONNECT_FAILED);
        goto err;
    }

    freeaddrinfo(local);
    freeaddrinfo(peer);
    return sock;

err:
    if (-1 != sock)
        closesocket(sock);
    if (local)
        freeaddrinfo(local);
    if (peer)
        freeaddrinfo(peer);
    return -1;
}

/* compatibility wrapper */
InetSocketAddress *inet_parse(const char *str, Error **errp)
{
    InetSocketAddress *addr;
    const char *optstr, *h;
    char host[64];
    char port[33];
    int to;
    int pos;

    addr = g_new0(InetSocketAddress, 1);

    /* parse address */
    if (str[0] == ':') {
        /* no host given */
        host[0] = '\0';
        if (1 != sscanf(str, ":%32[^,]%n", port, &pos)) {
            error_setg(errp, "error parsing port in address '%s'", str);
            goto fail;
        }
    } else if (str[0] == '[') {
        /* IPv6 addr */
        if (2 != sscanf(str, "[%64[^]]]:%32[^,]%n", host, port, &pos)) {
            error_setg(errp, "error parsing IPv6 address '%s'", str);
            goto fail;
        }
        addr->ipv6 = addr->has_ipv6 = true;
    } else {
        /* hostname or IPv4 addr */
        if (2 != sscanf(str, "%64[^:]:%32[^,]%n", host, port, &pos)) {
            error_setg(errp, "error parsing address '%s'", str);
            goto fail;
        }
        if (host[strspn(host, "0123456789.")] == '\0') {
            addr->ipv4 = addr->has_ipv4 = true;
        }
    }

    addr->host = g_strdup(host);
    addr->port = g_strdup(port);

    /* parse options */
    optstr = str + pos;
    h = strstr(optstr, ",to=");
    if (h) {
        h += 4;
        if (sscanf(h, "%d%n", &to, &pos) != 1 ||
            (h[pos] != '\0' && h[pos] != ',')) {
            error_setg(errp, "error parsing to= argument");
            goto fail;
        }
        addr->has_to = true;
        addr->to = to;
    }
    if (strstr(optstr, ",ipv4")) {
        addr->ipv4 = addr->has_ipv4 = true;
    }
    if (strstr(optstr, ",ipv6")) {
        addr->ipv6 = addr->has_ipv6 = true;
    }
    return addr;

fail:
    qapi_free_InetSocketAddress(addr);
    return NULL;
}

static void inet_addr_to_opts(QemuOpts *opts, const InetSocketAddress *addr)
{
    bool ipv4 = addr->ipv4 || !addr->has_ipv4;
    bool ipv6 = addr->ipv6 || !addr->has_ipv6;

    if (!ipv4 || !ipv6) {
        qemu_opt_set_bool(opts, "ipv4", ipv4);
        qemu_opt_set_bool(opts, "ipv6", ipv6);
    }
    if (addr->has_to) {
        char to[20];
        snprintf(to, sizeof(to), "%d", addr->to);
        qemu_opt_set(opts, "to", to);
    }
    qemu_opt_set(opts, "host", addr->host);
    qemu_opt_set(opts, "port", addr->port);
}

int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset, Error **errp)
{
    QemuOpts *opts;
    char *optstr;
    int sock = -1;
    InetSocketAddress *addr;

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
        inet_addr_to_opts(opts, addr);
        qapi_free_InetSocketAddress(addr);
        sock = inet_listen_opts(opts, port_offset, errp);
        if (sock != -1 && ostr) {
            optstr = strchr(str, ',');
            if (qemu_opt_get_bool(opts, "ipv6", 0)) {
                snprintf(ostr, olen, "[%s]:%s%s",
                         qemu_opt_get(opts, "host"),
                         qemu_opt_get(opts, "port"),
                         optstr ? optstr : "");
            } else {
                snprintf(ostr, olen, "%s:%s%s",
                         qemu_opt_get(opts, "host"),
                         qemu_opt_get(opts, "port"),
                         optstr ? optstr : "");
            }
        }
        qemu_opts_del(opts);
    }
    return sock;
}

/**
 * Create a blocking socket and connect it to an address.
 *
 * @str: address string
 * @errp: set in case of an error
 *
 * Returns -1 in case of error, file descriptor on success
 **/
int inet_connect(const char *str, Error **errp)
{
    QemuOpts *opts;
    int sock = -1;
    InetSocketAddress *addr;

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
        inet_addr_to_opts(opts, addr);
        qapi_free_InetSocketAddress(addr);
        sock = inet_connect_opts(opts, errp, NULL, NULL);
        qemu_opts_del(opts);
    }
    return sock;
}

/**
 * Create a non-blocking socket and connect it to an address.
 * Calls the callback function with fd in case of success or -1 in case of
 * error.
 *
 * @str: address string
 * @callback: callback function that is called when connect completes,
 *            cannot be NULL.
 * @opaque: opaque for callback function
 * @errp: set in case of an error
 *
 * Returns: -1 on immediate error, file descriptor on success.
 **/
int inet_nonblocking_connect(const char *str,
                             NonBlockingConnectHandler *callback,
                             void *opaque, Error **errp)
{
    QemuOpts *opts;
    int sock = -1;
    InetSocketAddress *addr;

    g_assert(callback != NULL);

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
        inet_addr_to_opts(opts, addr);
        qapi_free_InetSocketAddress(addr);
        sock = inet_connect_opts(opts, errp, callback, opaque);
        qemu_opts_del(opts);
    }
    return sock;
}

#ifndef _WIN32

int unix_listen_opts(QemuOpts *opts, Error **errp)
{
    struct sockaddr_un un;
    const char *path = qemu_opt_get(opts, "path");
    int sock, fd;

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CREATE_FAILED);
        return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    if (path && strlen(path)) {
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    } else {
        char *tmpdir = getenv("TMPDIR");
        snprintf(un.sun_path, sizeof(un.sun_path), "%s/qemu-socket-XXXXXX",
                 tmpdir ? tmpdir : "/tmp");
        /*
         * This dummy fd usage silences the mktemp() unsecure warning.
         * Using mkstemp() doesn't make things more secure here
         * though.  bind() complains about existing files, so we have
         * to unlink first and thus re-open the race window.  The
         * worst case possible is bind() failing, i.e. a DoS attack.
         */
        fd = mkstemp(un.sun_path); close(fd);
        qemu_opt_set(opts, "path", un.sun_path);
    }

    unlink(un.sun_path);
    if (bind(sock, (struct sockaddr*) &un, sizeof(un)) < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_BIND_FAILED);
        goto err;
    }
    if (listen(sock, 1) < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_LISTEN_FAILED);
        goto err;
    }

    return sock;

err:
    closesocket(sock);
    return -1;
}

int unix_connect_opts(QemuOpts *opts, Error **errp,
                      NonBlockingConnectHandler *callback, void *opaque)
{
    struct sockaddr_un un;
    const char *path = qemu_opt_get(opts, "path");
    ConnectState *connect_state = NULL;
    int sock, rc;

    if (NULL == path) {
        error_setg(errp, "unix connect: no path specified");
        return -1;
    }

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_set_errno(errp, errno, QERR_SOCKET_CREATE_FAILED);
        return -1;
    }
    if (callback != NULL) {
        connect_state = g_malloc0(sizeof(*connect_state));
        connect_state->callback = callback;
        connect_state->opaque = opaque;
        qemu_set_nonblock(sock);
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);

    /* connect to peer */
    do {
        rc = 0;
        if (connect(sock, (struct sockaddr *) &un, sizeof(un)) < 0) {
            rc = -socket_error();
        }
    } while (rc == -EINTR);

    if (connect_state != NULL && QEMU_SOCKET_RC_INPROGRESS(rc)) {
        connect_state->fd = sock;
        qemu_set_fd_handler2(sock, NULL, NULL, wait_for_connect,
                             connect_state);
        return sock;
    } else if (rc >= 0) {
        /* non blocking socket immediate success, call callback */
        if (callback != NULL) {
            callback(sock, opaque);
        }
    }

    if (rc < 0) {
        error_set_errno(errp, -rc, QERR_SOCKET_CONNECT_FAILED);
        close(sock);
        sock = -1;
    }

    g_free(connect_state);
    return sock;
}

#else

int unix_listen_opts(QemuOpts *opts, Error **errp)
{
    error_setg(errp, "unix sockets are not available on windows");
    errno = ENOTSUP;
    return -1;
}

int unix_connect_opts(QemuOpts *opts, Error **errp,
                      NonBlockingConnectHandler *callback, void *opaque)
{
    error_setg(errp, "unix sockets are not available on windows");
    errno = ENOTSUP;
    return -1;
}
#endif

/* compatibility wrapper */
int unix_listen(const char *str, char *ostr, int olen, Error **errp)
{
    QemuOpts *opts;
    char *path, *optstr;
    int sock, len;

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);

    optstr = strchr(str, ',');
    if (optstr) {
        len = optstr - str;
        if (len) {
            path = g_malloc(len+1);
            snprintf(path, len+1, "%.*s", len, str);
            qemu_opt_set(opts, "path", path);
            g_free(path);
        }
    } else {
        qemu_opt_set(opts, "path", str);
    }

    sock = unix_listen_opts(opts, errp);

    if (sock != -1 && ostr)
        snprintf(ostr, olen, "%s%s", qemu_opt_get(opts, "path"), optstr ? optstr : "");
    qemu_opts_del(opts);
    return sock;
}

int unix_connect(const char *path, Error **errp)
{
    QemuOpts *opts;
    int sock;

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
    qemu_opt_set(opts, "path", path);
    sock = unix_connect_opts(opts, errp, NULL, NULL);
    qemu_opts_del(opts);
    return sock;
}


int unix_nonblocking_connect(const char *path,
                             NonBlockingConnectHandler *callback,
                             void *opaque, Error **errp)
{
    QemuOpts *opts;
    int sock = -1;

    g_assert(callback != NULL);

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
    qemu_opt_set(opts, "path", path);
    sock = unix_connect_opts(opts, errp, callback, opaque);
    qemu_opts_del(opts);
    return sock;
}

SocketAddress *socket_parse(const char *str, Error **errp)
{
    SocketAddress *addr;

    addr = g_new0(SocketAddress, 1);
    if (strstart(str, "unix:", NULL)) {
        if (str[5] == '\0') {
            error_setg(errp, "invalid Unix socket address");
            goto fail;
        } else {
            addr->kind = SOCKET_ADDRESS_KIND_UNIX;
            addr->q_unix = g_new(UnixSocketAddress, 1);
            addr->q_unix->path = g_strdup(str + 5);
        }
    } else if (strstart(str, "fd:", NULL)) {
        if (str[3] == '\0') {
            error_setg(errp, "invalid file descriptor address");
            goto fail;
        } else {
            addr->kind = SOCKET_ADDRESS_KIND_FD;
            addr->fd = g_new(String, 1);
            addr->fd->str = g_strdup(str + 3);
        }
    } else {
        addr->kind = SOCKET_ADDRESS_KIND_INET;
        addr->inet = inet_parse(str, errp);
        if (addr->inet == NULL) {
            goto fail;
        }
    }
    return addr;

fail:
    qapi_free_SocketAddress(addr);
    return NULL;
}

int socket_connect(SocketAddress *addr, Error **errp,
                   NonBlockingConnectHandler *callback, void *opaque)
{
    QemuOpts *opts;
    int fd;

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
    switch (addr->kind) {
    case SOCKET_ADDRESS_KIND_INET:
        inet_addr_to_opts(opts, addr->inet);
        fd = inet_connect_opts(opts, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_UNIX:
        qemu_opt_set(opts, "path", addr->q_unix->path);
        fd = unix_connect_opts(opts, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_FD:
        fd = monitor_get_fd(cur_mon, addr->fd->str, errp);
        if (fd >= 0 && callback) {
            qemu_set_nonblock(fd);
            callback(fd, opaque);
        }
        break;

    default:
        abort();
    }
    qemu_opts_del(opts);
    return fd;
}

int socket_listen(SocketAddress *addr, Error **errp)
{
    QemuOpts *opts;
    int fd;

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
    switch (addr->kind) {
    case SOCKET_ADDRESS_KIND_INET:
        inet_addr_to_opts(opts, addr->inet);
        fd = inet_listen_opts(opts, 0, errp);
        break;

    case SOCKET_ADDRESS_KIND_UNIX:
        qemu_opt_set(opts, "path", addr->q_unix->path);
        fd = unix_listen_opts(opts, errp);
        break;

    case SOCKET_ADDRESS_KIND_FD:
        fd = monitor_get_fd(cur_mon, addr->fd->str, errp);
        break;

    default:
        abort();
    }
    qemu_opts_del(opts);
    return fd;
}

int socket_dgram(SocketAddress *remote, SocketAddress *local, Error **errp)
{
    QemuOpts *opts;
    int fd;

    opts = qemu_opts_create(&socket_optslist, NULL, 0, &error_abort);
    switch (remote->kind) {
    case SOCKET_ADDRESS_KIND_INET:
        qemu_opt_set(opts, "host", remote->inet->host);
        qemu_opt_set(opts, "port", remote->inet->port);
        if (local) {
            qemu_opt_set(opts, "localaddr", local->inet->host);
            qemu_opt_set(opts, "localport", local->inet->port);
        }
        fd = inet_dgram_opts(opts, errp);
        break;

    default:
        error_setg(errp, "socket type unsupported for datagram");
        fd = -1;
    }
    qemu_opts_del(opts);
    return fd;
}
