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
#include "qemu/osdep.h"

#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi-visit.h"
#include "qemu/cutils.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif

#ifndef AI_V4MAPPED
# define AI_V4MAPPED 0
#endif


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

NetworkAddressFamily inet_netfamily(int family)
{
    switch (family) {
    case PF_INET6: return NETWORK_ADDRESS_FAMILY_IPV6;
    case PF_INET:  return NETWORK_ADDRESS_FAMILY_IPV4;
    case PF_UNIX:  return NETWORK_ADDRESS_FAMILY_UNIX;
    }
    return NETWORK_ADDRESS_FAMILY_UNKNOWN;
}

/*
 * Matrix we're trying to apply
 *
 *  ipv4  ipv6   family
 *   -     -       PF_UNSPEC
 *   -     f       PF_INET
 *   -     t       PF_INET6
 *   f     -       PF_INET6
 *   f     f       <error>
 *   f     t       PF_INET6
 *   t     -       PF_INET
 *   t     f       PF_INET
 *   t     t       PF_INET6
 *
 * NB, this matrix is only about getting the necessary results
 * from getaddrinfo(). Some of the cases require further work
 * after reading results from getaddrinfo in order to fully
 * apply the logic the end user wants. eg with the last case
 * ipv4=t + ipv6=t + PF_INET6, getaddrinfo alone can only
 * guarantee the ipv6=t part of the request - we need more
 * checks to provide ipv4=t part of the guarantee. This is
 * outside scope of this method and not currently handled by
 * callers at all.
 */
static int inet_ai_family_from_address(InetSocketAddress *addr,
                                       Error **errp)
{
    if (addr->has_ipv6 && addr->has_ipv4 &&
        !addr->ipv6 && !addr->ipv4) {
        error_setg(errp, "Cannot disable IPv4 and IPv6 at same time");
        return PF_UNSPEC;
    }
    if ((addr->has_ipv6 && addr->ipv6) || (addr->has_ipv4 && !addr->ipv4)) {
        return PF_INET6;
    }
    if ((addr->has_ipv4 && addr->ipv4) || (addr->has_ipv6 && !addr->ipv6)) {
        return PF_INET;
    }
    return PF_UNSPEC;
}

static int inet_listen_saddr(InetSocketAddress *saddr,
                             int port_offset,
                             bool update_addr,
                             Error **errp)
{
    struct addrinfo ai,*res,*e;
    char port[33];
    char uaddr[INET6_ADDRSTRLEN+1];
    char uport[33];
    int slisten, rc, port_min, port_max, p;
    Error *err = NULL;

    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE;
    ai.ai_family = inet_ai_family_from_address(saddr, &err);
    ai.ai_socktype = SOCK_STREAM;

    if (err) {
        error_propagate(errp, err);
        return -1;
    }

    if (saddr->host == NULL) {
        error_setg(errp, "host not specified");
        return -1;
    }
    if (saddr->port != NULL) {
        pstrcpy(port, sizeof(port), saddr->port);
    } else {
        port[0] = '\0';
    }

    /* lookup */
    if (port_offset) {
        unsigned long long baseport;
        if (strlen(port) == 0) {
            error_setg(errp, "port not specified");
            return -1;
        }
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
    rc = getaddrinfo(strlen(saddr->host) ? saddr->host : NULL,
                     strlen(port) ? port : NULL, &ai, &res);
    if (rc != 0) {
        error_setg(errp, "address resolution failed for %s:%s: %s",
                   saddr->host, port, gai_strerror(rc));
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
                error_setg_errno(errp, errno, "Failed to create socket");
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
        port_max = saddr->has_to ? saddr->to + port_offset : port_min;
        for (p = port_min; p <= port_max; p++) {
            inet_setport(e, p);
            if (bind(slisten, e->ai_addr, e->ai_addrlen) == 0) {
                goto listen;
            }
            if (p == port_max) {
                if (!e->ai_next) {
                    error_setg_errno(errp, errno, "Failed to bind socket");
                }
            }
        }
        closesocket(slisten);
    }
    freeaddrinfo(res);
    return -1;

listen:
    if (listen(slisten,1) != 0) {
        error_setg_errno(errp, errno, "Failed to listen on socket");
        closesocket(slisten);
        freeaddrinfo(res);
        return -1;
    }
    if (update_addr) {
        g_free(saddr->host);
        saddr->host = g_strdup(uaddr);
        g_free(saddr->port);
        saddr->port = g_strdup_printf("%d",
                                      inet_getport(e) - port_offset);
        saddr->has_ipv6 = saddr->ipv6 = e->ai_family == PF_INET6;
        saddr->has_ipv4 = saddr->ipv4 = e->ai_family != PF_INET6;
    }
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
    Error *err = NULL;

    qemu_set_fd_handler(s->fd, NULL, NULL, NULL);

    do {
        rc = qemu_getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &val, &valsize);
    } while (rc == -1 && errno == EINTR);

    /* update rc to contain error */
    if (!rc && val) {
        rc = -1;
        errno = val;
    }

    /* connect error */
    if (rc < 0) {
        error_setg_errno(&err, errno, "Error connecting to socket");
        closesocket(s->fd);
        s->fd = rc;
    }

    /* try to connect to the next address on the list */
    if (s->current_addr) {
        while (s->current_addr->ai_next != NULL && s->fd < 0) {
            s->current_addr = s->current_addr->ai_next;
            s->fd = inet_connect_addr(s->current_addr, &in_progress, s, NULL);
            if (s->fd < 0) {
                error_free(err);
                err = NULL;
                error_setg_errno(&err, errno, "Unable to start socket connect");
            }
            /* connect in progress */
            if (in_progress) {
                goto out;
            }
        }

        freeaddrinfo(s->addr_list);
    }

    if (s->callback) {
        s->callback(s->fd, err, s->opaque);
    }
    g_free(s);
out:
    error_free(err);
}

static int inet_connect_addr(struct addrinfo *addr, bool *in_progress,
                             ConnectState *connect_state, Error **errp)
{
    int sock, rc;

    *in_progress = false;

    sock = qemu_socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
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
            rc = -errno;
        }
    } while (rc == -EINTR);

    if (connect_state != NULL && QEMU_SOCKET_RC_INPROGRESS(rc)) {
        connect_state->fd = sock;
        qemu_set_fd_handler(sock, NULL, wait_for_connect, connect_state);
        *in_progress = true;
    } else if (rc < 0) {
        error_setg_errno(errp, errno, "Failed to connect socket");
        closesocket(sock);
        return -1;
    }
    return sock;
}

static struct addrinfo *inet_parse_connect_saddr(InetSocketAddress *saddr,
                                                 Error **errp)
{
    struct addrinfo ai, *res;
    int rc;
    Error *err = NULL;
    static int useV4Mapped = 1;

    memset(&ai, 0, sizeof(ai));

    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    if (atomic_read(&useV4Mapped)) {
        ai.ai_flags |= AI_V4MAPPED;
    }
    ai.ai_family = inet_ai_family_from_address(saddr, &err);
    ai.ai_socktype = SOCK_STREAM;

    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    if (saddr->host == NULL || saddr->port == NULL) {
        error_setg(errp, "host and/or port not specified");
        return NULL;
    }

    /* lookup */
    rc = getaddrinfo(saddr->host, saddr->port, &ai, &res);

    /* At least FreeBSD and OS-X 10.6 declare AI_V4MAPPED but
     * then don't implement it in their getaddrinfo(). Detect
     * this and retry without the flag since that's preferrable
     * to a fatal error
     */
    if (rc == EAI_BADFLAGS &&
        (ai.ai_flags & AI_V4MAPPED)) {
        atomic_set(&useV4Mapped, 0);
        ai.ai_flags &= ~AI_V4MAPPED;
        rc = getaddrinfo(saddr->host, saddr->port, &ai, &res);
    }
    if (rc != 0) {
        error_setg(errp, "address resolution failed for %s:%s: %s",
                   saddr->host, saddr->port, gai_strerror(rc));
        return NULL;
    }
    return res;
}

/**
 * Create a socket and connect it to an address.
 *
 * @saddr: Inet socket address specification
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
static int inet_connect_saddr(InetSocketAddress *saddr, Error **errp,
                              NonBlockingConnectHandler *callback, void *opaque)
{
    Error *local_err = NULL;
    struct addrinfo *res, *e;
    int sock = -1;
    bool in_progress;
    ConnectState *connect_state = NULL;

    res = inet_parse_connect_saddr(saddr, errp);
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
            callback(sock, NULL, opaque);
        }
    }
    g_free(connect_state);
    freeaddrinfo(res);
    return sock;
}

static int inet_dgram_saddr(InetSocketAddress *sraddr,
                            InetSocketAddress *sladdr,
                            Error **errp)
{
    struct addrinfo ai, *peer = NULL, *local = NULL;
    const char *addr;
    const char *port;
    int sock = -1, rc;
    Error *err = NULL;

    /* lookup peer addr */
    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_V4MAPPED | AI_ADDRCONFIG;
    ai.ai_family = inet_ai_family_from_address(sraddr, &err);
    ai.ai_socktype = SOCK_DGRAM;

    if (err) {
        error_propagate(errp, err);
        goto err;
    }

    addr = sraddr->host;
    port = sraddr->port;
    if (addr == NULL || strlen(addr) == 0) {
        addr = "localhost";
    }
    if (port == NULL || strlen(port) == 0) {
        error_setg(errp, "remote port not specified");
        goto err;
    }

    if (0 != (rc = getaddrinfo(addr, port, &ai, &peer))) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
	goto err;
    }

    /* lookup local addr */
    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE;
    ai.ai_family = peer->ai_family;
    ai.ai_socktype = SOCK_DGRAM;

    if (sladdr) {
        addr = sladdr->host;
        port = sladdr->port;
        if (addr == NULL || strlen(addr) == 0) {
            addr = NULL;
        }
        if (!port || strlen(port) == 0) {
            port = "0";
        }
    } else {
        addr = NULL;
        port = "0";
    }

    if (0 != (rc = getaddrinfo(addr, port, &ai, &local))) {
        error_setg(errp, "address resolution failed for %s:%s: %s", addr, port,
                   gai_strerror(rc));
        goto err;
    }

    /* create socket */
    sock = qemu_socket(peer->ai_family, peer->ai_socktype, peer->ai_protocol);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
        goto err;
    }
    socket_set_fast_reuse(sock);

    /* bind socket */
    if (bind(sock, local->ai_addr, local->ai_addrlen) < 0) {
        error_setg_errno(errp, errno, "Failed to bind socket");
        goto err;
    }

    /* connect to peer */
    if (connect(sock,peer->ai_addr,peer->ai_addrlen) < 0) {
        error_setg_errno(errp, errno, "Failed to connect socket");
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
    char host[65];
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

int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset, Error **errp)
{
    char *optstr;
    int sock = -1;
    InetSocketAddress *addr;

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        sock = inet_listen_saddr(addr, port_offset, true, errp);
        if (sock != -1 && ostr) {
            optstr = strchr(str, ',');
            if (addr->ipv6) {
                snprintf(ostr, olen, "[%s]:%s%s",
                         addr->host,
                         addr->port,
                         optstr ? optstr : "");
            } else {
                snprintf(ostr, olen, "%s:%s%s",
                         addr->host,
                         addr->port,
                         optstr ? optstr : "");
            }
        }
        qapi_free_InetSocketAddress(addr);
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
    int sock = -1;
    InetSocketAddress *addr;

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        sock = inet_connect_saddr(addr, errp, NULL, NULL);
        qapi_free_InetSocketAddress(addr);
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
    int sock = -1;
    InetSocketAddress *addr;

    g_assert(callback != NULL);

    addr = inet_parse(str, errp);
    if (addr != NULL) {
        sock = inet_connect_saddr(addr, errp, callback, opaque);
        qapi_free_InetSocketAddress(addr);
    }
    return sock;
}

#ifndef _WIN32

static int unix_listen_saddr(UnixSocketAddress *saddr,
                             bool update_addr,
                             Error **errp)
{
    struct sockaddr_un un;
    int sock, fd;

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create Unix socket");
        return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    if (saddr->path && strlen(saddr->path)) {
        snprintf(un.sun_path, sizeof(un.sun_path), "%s", saddr->path);
    } else {
        const char *tmpdir = getenv("TMPDIR");
        tmpdir = tmpdir ? tmpdir : "/tmp";
        if (snprintf(un.sun_path, sizeof(un.sun_path), "%s/qemu-socket-XXXXXX",
                     tmpdir) >= sizeof(un.sun_path)) {
            error_setg_errno(errp, errno,
                             "TMPDIR environment variable (%s) too large", tmpdir);
            goto err;
        }

        /*
         * This dummy fd usage silences the mktemp() unsecure warning.
         * Using mkstemp() doesn't make things more secure here
         * though.  bind() complains about existing files, so we have
         * to unlink first and thus re-open the race window.  The
         * worst case possible is bind() failing, i.e. a DoS attack.
         */
        fd = mkstemp(un.sun_path);
        if (fd < 0) {
            error_setg_errno(errp, errno,
                             "Failed to make a temporary socket name in %s", tmpdir);
            goto err;
        }
        close(fd);
        if (update_addr) {
            g_free(saddr->path);
            saddr->path = g_strdup(un.sun_path);
        }
    }

    if (unlink(un.sun_path) < 0 && errno != ENOENT) {
        error_setg_errno(errp, errno,
                         "Failed to unlink socket %s", un.sun_path);
        goto err;
    }
    if (bind(sock, (struct sockaddr*) &un, sizeof(un)) < 0) {
        error_setg_errno(errp, errno, "Failed to bind socket to %s", un.sun_path);
        goto err;
    }
    if (listen(sock, 1) < 0) {
        error_setg_errno(errp, errno, "Failed to listen on socket");
        goto err;
    }

    return sock;

err:
    closesocket(sock);
    return -1;
}

static int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp,
                              NonBlockingConnectHandler *callback, void *opaque)
{
    struct sockaddr_un un;
    ConnectState *connect_state = NULL;
    int sock, rc;

    if (saddr->path == NULL) {
        error_setg(errp, "unix connect: no path specified");
        return -1;
    }

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
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
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", saddr->path);

    /* connect to peer */
    do {
        rc = 0;
        if (connect(sock, (struct sockaddr *) &un, sizeof(un)) < 0) {
            rc = -errno;
        }
    } while (rc == -EINTR);

    if (connect_state != NULL && QEMU_SOCKET_RC_INPROGRESS(rc)) {
        connect_state->fd = sock;
        qemu_set_fd_handler(sock, NULL, wait_for_connect, connect_state);
        return sock;
    } else if (rc >= 0) {
        /* non blocking socket immediate success, call callback */
        if (callback != NULL) {
            callback(sock, NULL, opaque);
        }
    }

    if (rc < 0) {
        error_setg_errno(errp, -rc, "Failed to connect socket");
        close(sock);
        sock = -1;
    }

    g_free(connect_state);
    return sock;
}

#else

static int unix_listen_saddr(UnixSocketAddress *saddr,
                             bool update_addr,
                             Error **errp)
{
    error_setg(errp, "unix sockets are not available on windows");
    errno = ENOTSUP;
    return -1;
}

static int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp,
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
    char *path, *optstr;
    int sock, len;
    UnixSocketAddress *saddr;

    saddr = g_new0(UnixSocketAddress, 1);

    optstr = strchr(str, ',');
    if (optstr) {
        len = optstr - str;
        if (len) {
            path = g_malloc(len+1);
            snprintf(path, len+1, "%.*s", len, str);
            saddr->path = path;
        }
    } else {
        saddr->path = g_strdup(str);
    }

    sock = unix_listen_saddr(saddr, true, errp);

    if (sock != -1 && ostr)
        snprintf(ostr, olen, "%s%s", saddr->path, optstr ? optstr : "");
    qapi_free_UnixSocketAddress(saddr);
    return sock;
}

int unix_connect(const char *path, Error **errp)
{
    UnixSocketAddress *saddr;
    int sock;

    saddr = g_new0(UnixSocketAddress, 1);
    saddr->path = g_strdup(path);
    sock = unix_connect_saddr(saddr, errp, NULL, NULL);
    qapi_free_UnixSocketAddress(saddr);
    return sock;
}


int unix_nonblocking_connect(const char *path,
                             NonBlockingConnectHandler *callback,
                             void *opaque, Error **errp)
{
    UnixSocketAddress *saddr;
    int sock = -1;

    g_assert(callback != NULL);

    saddr = g_new0(UnixSocketAddress, 1);
    saddr->path = g_strdup(path);
    sock = unix_connect_saddr(saddr, errp, callback, opaque);
    qapi_free_UnixSocketAddress(saddr);
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
            addr->type = SOCKET_ADDRESS_KIND_UNIX;
            addr->u.q_unix.data = g_new(UnixSocketAddress, 1);
            addr->u.q_unix.data->path = g_strdup(str + 5);
        }
    } else if (strstart(str, "fd:", NULL)) {
        if (str[3] == '\0') {
            error_setg(errp, "invalid file descriptor address");
            goto fail;
        } else {
            addr->type = SOCKET_ADDRESS_KIND_FD;
            addr->u.fd.data = g_new(String, 1);
            addr->u.fd.data->str = g_strdup(str + 3);
        }
    } else {
        addr->type = SOCKET_ADDRESS_KIND_INET;
        addr->u.inet.data = inet_parse(str, errp);
        if (addr->u.inet.data == NULL) {
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
    int fd;

    switch (addr->type) {
    case SOCKET_ADDRESS_KIND_INET:
        fd = inet_connect_saddr(addr->u.inet.data, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_UNIX:
        fd = unix_connect_saddr(addr->u.q_unix.data, errp, callback, opaque);
        break;

    case SOCKET_ADDRESS_KIND_FD:
        fd = monitor_get_fd(cur_mon, addr->u.fd.data->str, errp);
        if (fd >= 0 && callback) {
            qemu_set_nonblock(fd);
            callback(fd, NULL, opaque);
        }
        break;

    default:
        abort();
    }
    return fd;
}

int socket_listen(SocketAddress *addr, Error **errp)
{
    int fd;

    switch (addr->type) {
    case SOCKET_ADDRESS_KIND_INET:
        fd = inet_listen_saddr(addr->u.inet.data, 0, false, errp);
        break;

    case SOCKET_ADDRESS_KIND_UNIX:
        fd = unix_listen_saddr(addr->u.q_unix.data, false, errp);
        break;

    case SOCKET_ADDRESS_KIND_FD:
        fd = monitor_get_fd(cur_mon, addr->u.fd.data->str, errp);
        break;

    default:
        abort();
    }
    return fd;
}

int socket_dgram(SocketAddress *remote, SocketAddress *local, Error **errp)
{
    int fd;

    switch (remote->type) {
    case SOCKET_ADDRESS_KIND_INET:
        fd = inet_dgram_saddr(remote->u.inet.data,
                              local ? local->u.inet.data : NULL, errp);
        break;

    default:
        error_setg(errp, "socket type unsupported for datagram");
        fd = -1;
    }
    return fd;
}


static SocketAddress *
socket_sockaddr_to_address_inet(struct sockaddr_storage *sa,
                                socklen_t salen,
                                Error **errp)
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    SocketAddress *addr;
    InetSocketAddress *inet;
    int ret;

    ret = getnameinfo((struct sockaddr *)sa, salen,
                      host, sizeof(host),
                      serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret != 0) {
        error_setg(errp, "Cannot format numeric socket address: %s",
                   gai_strerror(ret));
        return NULL;
    }

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_KIND_INET;
    inet = addr->u.inet.data = g_new0(InetSocketAddress, 1);
    inet->host = g_strdup(host);
    inet->port = g_strdup(serv);
    if (sa->ss_family == AF_INET) {
        inet->has_ipv4 = inet->ipv4 = true;
    } else {
        inet->has_ipv6 = inet->ipv6 = true;
    }

    return addr;
}


#ifndef WIN32
static SocketAddress *
socket_sockaddr_to_address_unix(struct sockaddr_storage *sa,
                                socklen_t salen,
                                Error **errp)
{
    SocketAddress *addr;
    struct sockaddr_un *su = (struct sockaddr_un *)sa;

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_KIND_UNIX;
    addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
    if (su->sun_path[0]) {
        addr->u.q_unix.data->path = g_strndup(su->sun_path,
                                              sizeof(su->sun_path));
    }

    return addr;
}
#endif /* WIN32 */

SocketAddress *
socket_sockaddr_to_address(struct sockaddr_storage *sa,
                           socklen_t salen,
                           Error **errp)
{
    switch (sa->ss_family) {
    case AF_INET:
    case AF_INET6:
        return socket_sockaddr_to_address_inet(sa, salen, errp);

#ifndef WIN32
    case AF_UNIX:
        return socket_sockaddr_to_address_unix(sa, salen, errp);
#endif /* WIN32 */

    default:
        error_setg(errp, "socket family %d unsupported",
                   sa->ss_family);
        return NULL;
    }
    return 0;
}


SocketAddress *socket_local_address(int fd, Error **errp)
{
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);

    if (getsockname(fd, (struct sockaddr *)&ss, &sslen) < 0) {
        error_setg_errno(errp, errno, "%s",
                         "Unable to query local socket address");
        return NULL;
    }

    return socket_sockaddr_to_address(&ss, sslen, errp);
}


SocketAddress *socket_remote_address(int fd, Error **errp)
{
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);

    if (getpeername(fd, (struct sockaddr *)&ss, &sslen) < 0) {
        error_setg_errno(errp, errno, "%s",
                         "Unable to query remote socket address");
        return NULL;
    }

    return socket_sockaddr_to_address(&ss, sslen, errp);
}


void qapi_copy_SocketAddress(SocketAddress **p_dest,
                             SocketAddress *src)
{
    QmpOutputVisitor *qov;
    QmpInputVisitor *qiv;
    Visitor *ov, *iv;
    QObject *obj;

    *p_dest = NULL;

    qov = qmp_output_visitor_new();
    ov = qmp_output_get_visitor(qov);
    visit_type_SocketAddress(ov, NULL, &src, &error_abort);
    obj = qmp_output_get_qobject(qov);
    qmp_output_visitor_cleanup(qov);
    if (!obj) {
        return;
    }

    qiv = qmp_input_visitor_new(obj, true);
    iv = qmp_input_get_visitor(qiv);
    visit_type_SocketAddress(iv, NULL, p_dest, &error_abort);
    qmp_input_visitor_cleanup(qiv);
    qobject_decref(obj);
}
