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

#define SCALE_MS 1000000

#ifdef _WIN32
int slirp_closesocket(int fd);
int slirp_ioctlsocket(int fd, int req, void *val);
int inet_aton(const char *cp, struct in_addr *ia);
#define slirp_getsockopt(sockfd, level, optname, optval, optlen) \
    getsockopt(sockfd, level, optname, (void *)optval, optlen)
#define slirp_setsockopt(sockfd, level, optname, optval, optlen)        \
    setsockopt(sockfd, level, optname, (const void *)optval, optlen)
#define slirp_recv(sockfd, buf, len, flags) recv(sockfd, (void *)buf, len, flags)
#else
#define slirp_setsockopt setsockopt
#define slirp_getsockopt getsockopt
#define slirp_recv recv
#define slirp_closesocket close
#define slirp_ioctlsocket ioctl
#endif

int slirp_socket(int domain, int type, int protocol);
void slirp_set_nonblock(int fd);

static inline int slirp_socket_set_nodelay(int fd)
{
    int v = 1;
    return slirp_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

static inline int slirp_socket_set_fast_reuse(int fd)
{
#ifndef _WIN32
    int v = 1;
    return slirp_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
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
