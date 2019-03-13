/* SPDX-License-Identifier: MIT */
/*
 * util.c (mostly based on QEMU os-win32.c)
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010-2016 Red Hat, Inc.
 *
 * QEMU library functions for win32 which are shared between QEMU and
 * the QEMU tools.
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
#include "util.h"

#include <glib.h>
#include <fcntl.h>
#include <stdint.h>

#if defined(_WIN32)
int slirp_inet_aton(const char *cp, struct in_addr *ia)
{
    uint32_t addr = inet_addr(cp);
    if (addr == 0xffffffff) {
        return 0;
    }
    ia->s_addr = addr;
    return 1;
}
#endif

void slirp_set_nonblock(int fd)
{
#ifndef _WIN32
    int f;
    f = fcntl(fd, F_GETFL);
    assert(f != -1);
    f = fcntl(fd, F_SETFL, f | O_NONBLOCK);
    assert(f != -1);
#else
    unsigned long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);
#endif
}

static void slirp_set_cloexec(int fd)
{
#ifndef _WIN32
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f | FD_CLOEXEC);
    assert(f != -1);
#endif
}

/*
 * Opens a socket with FD_CLOEXEC set
 */
int slirp_socket(int domain, int type, int protocol)
{
    int ret;

#ifdef SOCK_CLOEXEC
    ret = socket(domain, type | SOCK_CLOEXEC, protocol);
    if (ret != -1 || errno != EINVAL) {
        return ret;
    }
#endif
    ret = socket(domain, type, protocol);
    if (ret >= 0) {
        slirp_set_cloexec(ret);
    }

    return ret;
}

#ifdef _WIN32
static int socket_error(void)
{
    switch (WSAGetLastError()) {
    case 0:
        return 0;
    case WSAEINTR:
        return EINTR;
    case WSAEINVAL:
        return EINVAL;
    case WSA_INVALID_HANDLE:
        return EBADF;
    case WSA_NOT_ENOUGH_MEMORY:
        return ENOMEM;
    case WSA_INVALID_PARAMETER:
        return EINVAL;
    case WSAENAMETOOLONG:
        return ENAMETOOLONG;
    case WSAENOTEMPTY:
        return ENOTEMPTY;
    case WSAEWOULDBLOCK:
         /* not using EWOULDBLOCK as we don't want code to have
          * to check both EWOULDBLOCK and EAGAIN */
        return EAGAIN;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAENOTSOCK:
        return ENOTSOCK;
    case WSAEDESTADDRREQ:
        return EDESTADDRREQ;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAEPROTOTYPE:
        return EPROTOTYPE;
    case WSAENOPROTOOPT:
        return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:
        return EOPNOTSUPP;
    case WSAEAFNOSUPPORT:
        return EAFNOSUPPORT;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAENETRESET:
        return ENETRESET;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAENOBUFS:
        return ENOBUFS;
    case WSAEISCONN:
        return EISCONN;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAELOOP:
        return ELOOP;
    case WSAEHOSTUNREACH:
        return EHOSTUNREACH;
    default:
        return EIO;
    }
}

#undef ioctlsocket
int slirp_ioctlsocket_wrap(int fd, int req, void *val)
{
    int ret;
    ret = ioctlsocket(fd, req, val);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef closesocket
int slirp_closesocket_wrap(int fd)
{
    int ret;
    ret = closesocket(fd);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef connect
int slirp_connect_wrap(int sockfd, const struct sockaddr *addr, int addrlen)
{
    int ret;
    ret = connect(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef listen
int slirp_listen_wrap(int sockfd, int backlog)
{
    int ret;
    ret = listen(sockfd, backlog);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef bind
int slirp_bind_wrap(int sockfd, const struct sockaddr *addr, int addrlen)
{
    int ret;
    ret = bind(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef socket
int slirp_socket_wrap(int domain, int type, int protocol)
{
    int ret;
    ret = socket(domain, type, protocol);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef accept
int slirp_accept_wrap(int sockfd, struct sockaddr *addr, int *addrlen)
{
    int ret;
    ret = accept(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef shutdown
int slirp_shutdown_wrap(int sockfd, int how)
{
    int ret;
    ret = shutdown(sockfd, how);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef getsockopt
int slirp_getsockopt_wrap(int sockfd, int level, int optname,
                          void *optval, int *optlen)
{
    int ret;
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef setsockopt
int slirp_setsockopt_wrap(int sockfd, int level, int optname,
                          const void *optval, int optlen)
{
    int ret;
    ret = setsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef getpeername
int slirp_getpeername_wrap(int sockfd, struct sockaddr *addr,
                           int *addrlen)
{
    int ret;
    ret = getpeername(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef getsockname
int slirp_getsockname_wrap(int sockfd, struct sockaddr *addr,
                           int *addrlen)
{
    int ret;
    ret = getsockname(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef send
ssize_t slirp_send_wrap(int sockfd, const void *buf, size_t len, int flags)
{
    int ret;
    ret = send(sockfd, buf, len, flags);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef sendto
ssize_t slirp_sendto_wrap(int sockfd, const void *buf, size_t len, int flags,
                     const struct sockaddr *addr, int addrlen)
{
    int ret;
    ret = sendto(sockfd, buf, len, flags, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef recv
ssize_t slirp_recv_wrap(int sockfd, void *buf, size_t len, int flags)
{
    int ret;
    ret = recv(sockfd, buf, len, flags);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

#undef recvfrom
ssize_t slirp_recvfrom_wrap(int sockfd, void *buf, size_t len, int flags,
                            struct sockaddr *addr, int *addrlen)
{
    int ret;
    ret = recvfrom(sockfd, buf, len, flags, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}
#endif /* WIN32 */

void slirp_pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}
