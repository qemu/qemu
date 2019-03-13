/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010-2019 Red Hat, Inc.
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
#ifndef UTIL_H_
#define UTIL_H_

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#endif

#if defined(_WIN32)
# define SLIRP_PACKED __attribute__((gcc_struct, packed))
#else
# define SLIRP_PACKED __attribute__((packed))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#ifndef container_of
#define container_of(ptr, type, member) __extension__ ({    \
    void *__mptr = (void *)(ptr);               \
    ((type *)(__mptr - offsetof(type, member))); })
#endif

#if defined(_WIN32) /* CONFIG_IOVEC */
# if !defined(IOV_MAX) /* XXX: to avoid duplicate with QEMU osdep.h */
struct iovec {
    void *iov_base;
    size_t iov_len;
};
# endif
#else
#include <sys/uio.h>
#endif

#define SCALE_MS 1000000

#define ETH_ALEN    6
#define ETH_HLEN    14
#define ETH_P_IP                  (0x0800)      /* Internet Protocol packet  */
#define ETH_P_ARP                 (0x0806)      /* Address Resolution packet */
#define ETH_P_IPV6                (0x86dd)
#define ETH_P_VLAN                (0x8100)
#define ETH_P_DVLAN               (0x88a8)
#define ETH_P_NCSI                (0x88f8)
#define ETH_P_UNKNOWN             (0xffff)

/* FIXME: remove me when made standalone */
#ifdef _WIN32
#undef accept
#undef bind
#undef closesocket
#undef connect
#undef getpeername
#undef getsockname
#undef getsockopt
#undef ioctlsocket
#undef listen
#undef recv
#undef recvfrom
#undef send
#undef sendto
#undef setsockopt
#undef shutdown
#undef socket
#endif

#ifdef _WIN32
#define connect slirp_connect_wrap
int slirp_connect_wrap(int fd, const struct sockaddr *addr, int addrlen);
#define listen slirp_listen_wrap
int slirp_listen_wrap(int fd, int backlog);
#define bind slirp_bind_wrap
int slirp_bind_wrap(int fd, const struct sockaddr *addr, int addrlen);
#define socket slirp_socket_wrap
int slirp_socket_wrap(int domain, int type, int protocol);
#define accept slirp_accept_wrap
int slirp_accept_wrap(int fd, struct sockaddr *addr, int *addrlen);
#define shutdown slirp_shutdown_wrap
int slirp_shutdown_wrap(int fd, int how);
#define getpeername slirp_getpeername_wrap
int slirp_getpeername_wrap(int fd, struct sockaddr *addr, int *addrlen);
#define getsockname slirp_getsockname_wrap
int slirp_getsockname_wrap(int fd, struct sockaddr *addr, int *addrlen);
#define send slirp_send_wrap
ssize_t slirp_send_wrap(int fd, const void *buf, size_t len, int flags);
#define sendto slirp_sendto_wrap
ssize_t slirp_sendto_wrap(int fd, const void *buf, size_t len, int flags,
                          const struct sockaddr *dest_addr, int addrlen);
#define recv slirp_recv_wrap
ssize_t slirp_recv_wrap(int fd, void *buf, size_t len, int flags);
#define recvfrom slirp_recvfrom_wrap
ssize_t slirp_recvfrom_wrap(int fd, void *buf, size_t len, int flags,
                            struct sockaddr *src_addr, int *addrlen);
#define closesocket slirp_closesocket_wrap
int slirp_closesocket_wrap(int fd);
#define ioctlsocket slirp_ioctlsocket_wrap
int slirp_ioctlsocket_wrap(int fd, int req, void *val);
#define getsockopt slirp_getsockopt_wrap
int slirp_getsockopt_wrap(int sockfd, int level, int optname,
                     void *optval, int *optlen);
#define setsockopt slirp_setsockopt_wrap
int slirp_setsockopt_wrap(int sockfd, int level, int optname,
                          const void *optval, int optlen);
#define inet_aton slirp_inet_aton
int slirp_inet_aton(const char *cp, struct in_addr *ia);
#else
#define closesocket(s) close(s)
#define ioctlsocket(s, r, v) ioctl(s, r, v)
#endif

int slirp_socket(int domain, int type, int protocol);
void slirp_set_nonblock(int fd);

static inline int slirp_socket_set_nodelay(int fd)
{
    int v = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

static inline int slirp_socket_set_fast_reuse(int fd)
{
#ifndef _WIN32
    int v = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
#else
    /* Enabling the reuse of an endpoint that was used by a socket still in
     * TIME_WAIT state is usually performed by setting SO_REUSEADDR. On Windows
     * fast reuse is the default and SO_REUSEADDR does strange things. So we
     * don't have to do anything here. More info can be found at:
     * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740621.aspx */
    return 0;
#endif
}

void slirp_pstrcpy(char *buf, int buf_size, const char *str);

#endif
