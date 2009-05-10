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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "qemu_socket.h"
#include "qemu-common.h" /* for qemu_isdigit */

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif

static int sockets_debug = 0;
static const int on=1, off=0;

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

static const char *inet_strfamily(int family)
{
    switch (family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
    case PF_UNIX:  return "unix";
    }
    return "????";
}

static void inet_print_addrinfo(const char *tag, struct addrinfo *res)
{
    struct addrinfo *e;
    char uaddr[INET6_ADDRSTRLEN+1];
    char uport[33];

    for (e = res; e != NULL; e = e->ai_next) {
        getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
                    uaddr,INET6_ADDRSTRLEN,uport,32,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        fprintf(stderr,"%s: getaddrinfo: family %s, host %s, port %s\n",
                tag, inet_strfamily(e->ai_family), uaddr, uport);
    }
}

int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset)
{
    struct addrinfo ai,*res,*e;
    char addr[64];
    char port[33];
    char uaddr[INET6_ADDRSTRLEN+1];
    char uport[33];
    const char *opts, *h;
    int slisten,rc,pos,to,try_next;

    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = socktype;

    /* parse address */
    if (str[0] == ':') {
        /* no host given */
        addr[0] = '\0';
        if (1 != sscanf(str,":%32[^,]%n",port,&pos)) {
            fprintf(stderr, "%s: portonly parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
    } else if (str[0] == '[') {
        /* IPv6 addr */
        if (2 != sscanf(str,"[%64[^]]]:%32[^,]%n",addr,port,&pos)) {
            fprintf(stderr, "%s: ipv6 parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
        ai.ai_family = PF_INET6;
    } else if (qemu_isdigit(str[0])) {
        /* IPv4 addr */
        if (2 != sscanf(str,"%64[0-9.]:%32[^,]%n",addr,port,&pos)) {
            fprintf(stderr, "%s: ipv4 parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
        ai.ai_family = PF_INET;
    } else {
        /* hostname */
        if (2 != sscanf(str,"%64[^:]:%32[^,]%n",addr,port,&pos)) {
            fprintf(stderr, "%s: hostname parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
    }

    /* parse options */
    opts = str + pos;
    h = strstr(opts, ",to=");
    to = h ? atoi(h+4) : 0;
    if (strstr(opts, ",ipv4"))
        ai.ai_family = PF_INET;
    if (strstr(opts, ",ipv6"))
        ai.ai_family = PF_INET6;

    /* lookup */
    if (port_offset)
        snprintf(port, sizeof(port), "%d", atoi(port) + port_offset);
    rc = getaddrinfo(strlen(addr) ? addr : NULL, port, &ai, &res);
    if (rc != 0) {
        fprintf(stderr,"%s: getaddrinfo(%s,%s): %s\n", __FUNCTION__,
                addr, port, gai_strerror(rc));
        return -1;
    }
    if (sockets_debug)
        inet_print_addrinfo(__FUNCTION__, res);

    /* create socket + bind */
    for (e = res; e != NULL; e = e->ai_next) {
        getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
		        uaddr,INET6_ADDRSTRLEN,uport,32,
		        NI_NUMERICHOST | NI_NUMERICSERV);
        slisten = socket(e->ai_family, e->ai_socktype, e->ai_protocol);
        if (slisten < 0) {
            fprintf(stderr,"%s: socket(%s): %s\n", __FUNCTION__,
                    inet_strfamily(e->ai_family), strerror(errno));
            continue;
        }

        setsockopt(slisten,SOL_SOCKET,SO_REUSEADDR,(void*)&on,sizeof(on));
#ifdef IPV6_V6ONLY
        if (e->ai_family == PF_INET6) {
            /* listen on both ipv4 and ipv6 */
            setsockopt(slisten,IPPROTO_IPV6,IPV6_V6ONLY,(void*)&off,
                sizeof(off));
        }
#endif

        for (;;) {
            if (bind(slisten, e->ai_addr, e->ai_addrlen) == 0) {
                if (sockets_debug)
                    fprintf(stderr,"%s: bind(%s,%s,%d): OK\n", __FUNCTION__,
                        inet_strfamily(e->ai_family), uaddr, inet_getport(e));
                goto listen;
            }
            try_next = to && (inet_getport(e) <= to + port_offset);
            if (!try_next || sockets_debug)
                fprintf(stderr,"%s: bind(%s,%s,%d): %s\n", __FUNCTION__,
                        inet_strfamily(e->ai_family), uaddr, inet_getport(e),
                        strerror(errno));
            if (try_next) {
                inet_setport(e, inet_getport(e) + 1);
                continue;
            }
            break;
        }
        closesocket(slisten);
    }
    fprintf(stderr, "%s: FAILED\n", __FUNCTION__);
    freeaddrinfo(res);
    return -1;

listen:
    if (listen(slisten,1) != 0) {
        perror("listen");
        closesocket(slisten);
        freeaddrinfo(res);
        return -1;
    }
    if (ostr) {
        if (e->ai_family == PF_INET6) {
            snprintf(ostr, olen, "[%s]:%d%s", uaddr,
                     inet_getport(e) - port_offset, opts);
        } else {
            snprintf(ostr, olen, "%s:%d%s", uaddr,
                     inet_getport(e) - port_offset, opts);
        }
    }
    freeaddrinfo(res);
    return slisten;
}

int inet_connect(const char *str, int socktype)
{
    struct addrinfo ai,*res,*e;
    char addr[64];
    char port[33];
    char uaddr[INET6_ADDRSTRLEN+1];
    char uport[33];
    int sock,rc;

    memset(&ai,0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = PF_UNSPEC;
    ai.ai_socktype = socktype;

    /* parse address */
    if (str[0] == '[') {
        /* IPv6 addr */
        if (2 != sscanf(str,"[%64[^]]]:%32[^,]",addr,port)) {
            fprintf(stderr, "%s: ipv6 parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
        ai.ai_family = PF_INET6;
    } else if (qemu_isdigit(str[0])) {
        /* IPv4 addr */
        if (2 != sscanf(str,"%64[0-9.]:%32[^,]",addr,port)) {
            fprintf(stderr, "%s: ipv4 parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
        ai.ai_family = PF_INET;
    } else {
        /* hostname */
        if (2 != sscanf(str,"%64[^:]:%32[^,]",addr,port)) {
            fprintf(stderr, "%s: hostname parse error (%s)\n",
                    __FUNCTION__, str);
            return -1;
        }
    }

    /* parse options */
    if (strstr(str, ",ipv4"))
        ai.ai_family = PF_INET;
    if (strstr(str, ",ipv6"))
        ai.ai_family = PF_INET6;

    /* lookup */
    if (0 != (rc = getaddrinfo(addr, port, &ai, &res))) {
        fprintf(stderr,"getaddrinfo(%s,%s): %s\n", gai_strerror(rc),
                addr, port);
	return -1;
    }
    if (sockets_debug)
        inet_print_addrinfo(__FUNCTION__, res);

    for (e = res; e != NULL; e = e->ai_next) {
        if (getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
                            uaddr,INET6_ADDRSTRLEN,uport,32,
                            NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
            fprintf(stderr,"%s: getnameinfo: oops\n", __FUNCTION__);
            continue;
        }
        sock = socket(e->ai_family, e->ai_socktype, e->ai_protocol);
        if (sock < 0) {
            fprintf(stderr,"%s: socket(%s): %s\n", __FUNCTION__,
            inet_strfamily(e->ai_family), strerror(errno));
            continue;
        }
        setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(void*)&on,sizeof(on));

        /* connect to peer */
        if (connect(sock,e->ai_addr,e->ai_addrlen) < 0) {
            if (sockets_debug || NULL == e->ai_next)
                fprintf(stderr, "%s: connect(%s,%s,%s,%s): %s\n", __FUNCTION__,
                        inet_strfamily(e->ai_family),
                        e->ai_canonname, uaddr, uport, strerror(errno));
            closesocket(sock);
            continue;
        }
        if (sockets_debug)
            fprintf(stderr, "%s: connect(%s,%s,%s,%s): OK\n", __FUNCTION__,
                    inet_strfamily(e->ai_family),
                    e->ai_canonname, uaddr, uport);
        freeaddrinfo(res);
        return sock;
    }
    freeaddrinfo(res);
    return -1;
}

#ifndef _WIN32

int unix_listen(const char *str, char *ostr, int olen)
{
    struct sockaddr_un un;
    char *path, *opts;
    int sock, fd, len;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket(unix)");
        return -1;
    }

    opts = strchr(str, ',');
    if (opts) {
        len = opts - str;
        path = qemu_malloc(len+1);
        snprintf(path, len+1, "%.*s", len, str);
    } else
        path = qemu_strdup(str);

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
    }
    snprintf(ostr, olen, "%s%s", un.sun_path, opts ? opts : "");

    unlink(un.sun_path);
    if (bind(sock, (struct sockaddr*) &un, sizeof(un)) < 0) {
        fprintf(stderr, "bind(unix:%s): %s\n", un.sun_path, strerror(errno));
        goto err;
    }
    if (listen(sock, 1) < 0) {
        fprintf(stderr, "listen(unix:%s): %s\n", un.sun_path, strerror(errno));
        goto err;
    }

    if (sockets_debug)
        fprintf(stderr, "bind(unix:%s): OK\n", un.sun_path);
    qemu_free(path);
    return sock;

err:
    qemu_free(path);
    closesocket(sock);
    return -1;
}

int unix_connect(const char *path)
{
    struct sockaddr_un un;
    int sock;

    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket(unix)");
        return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    snprintf(un.sun_path, sizeof(un.sun_path), "%s", path);
    if (connect(sock, (struct sockaddr*) &un, sizeof(un)) < 0) {
        fprintf(stderr, "connect(unix:%s): %s\n", path, strerror(errno));
	return -1;
    }

    if (sockets_debug)
        fprintf(stderr, "connect(unix:%s): OK\n", path);
    return sock;
}

#else

int unix_listen(const char *path, char *ostr, int olen)
{
    fprintf(stderr, "unix sockets are not available on windows\n");
    return -1;
}

int unix_connect(const char *path)
{
    fprintf(stderr, "unix sockets are not available on windows\n");
    return -1;
}

#endif
