/*
 * win32 specific declarations
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Jes Sorensen <Jes.Sorensen@redhat.com>
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

#ifndef QEMU_OS_WIN32_H
#define QEMU_OS_WIN32_H

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#if defined(_WIN64)
/* On w64, setjmp is implemented by _setjmp which needs a second parameter.
 * If this parameter is NULL, longjump does no stack unwinding.
 * That is what we need for QEMU. Passing the value of register rsp (default)
 * lets longjmp try a stack unwinding which will crash with generated code. */
# undef setjmp
# define setjmp(env) _setjmp(env, NULL)
#endif
/* QEMU uses sigsetjmp()/siglongjmp() as the portable way to specify
 * "longjmp and don't touch the signal masks". Since we know that the
 * savemask parameter will always be zero we can safely define these
 * in terms of setjmp/longjmp on Win32.
 */
#define sigjmp_buf jmp_buf
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)

/* Missing POSIX functions. Don't use MinGW-w64 macros. */
#ifndef CONFIG_LOCALTIME_R
#undef gmtime_r
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#undef localtime_r
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif /* CONFIG_LOCALTIME_R */

static inline void os_setup_signal_handling(void) {}
static inline void os_daemonize(void) {}
static inline void os_setup_post(void) {}
void os_set_line_buffering(void);
static inline void os_set_proc_name(const char *dummy) {}

int getpagesize(void);

#if !defined(EPROTONOSUPPORT)
# define EPROTONOSUPPORT EINVAL
#endif

int setenv(const char *name, const char *value, int overwrite);

typedef struct {
    long tv_sec;
    long tv_usec;
} qemu_timeval;
int qemu_gettimeofday(qemu_timeval *tp);

static inline bool is_daemonized(void)
{
    return false;
}

static inline int os_mlock(void)
{
    return -ENOSYS;
}

#define fsync _commit

#if !defined(lseek)
# define lseek _lseeki64
#endif

int qemu_ftruncate64(int, int64_t);

#if !defined(ftruncate)
# define ftruncate qemu_ftruncate64
#endif

static inline char *realpath(const char *path, char *resolved_path)
{
    _fullpath(resolved_path, path, _MAX_PATH);
    return resolved_path;
}


/* We wrap all the sockets functions so that we can
 * set errno based on WSAGetLastError()
 */

#undef connect
#define connect qemu_connect_wrap
int qemu_connect_wrap(int sockfd, const struct sockaddr *addr,
                      socklen_t addrlen);

#undef listen
#define listen qemu_listen_wrap
int qemu_listen_wrap(int sockfd, int backlog);

#undef bind
#define bind qemu_bind_wrap
int qemu_bind_wrap(int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen);

#undef socket
#define socket qemu_socket_wrap
int qemu_socket_wrap(int domain, int type, int protocol);

#undef accept
#define accept qemu_accept_wrap
int qemu_accept_wrap(int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen);

#undef shutdown
#define shutdown qemu_shutdown_wrap
int qemu_shutdown_wrap(int sockfd, int how);

#undef ioctlsocket
#define ioctlsocket qemu_ioctlsocket_wrap
int qemu_ioctlsocket_wrap(int fd, int req, void *val);

#undef closesocket
#define closesocket qemu_closesocket_wrap
int qemu_closesocket_wrap(int fd);

#undef getsockopt
#define getsockopt qemu_getsockopt_wrap
int qemu_getsockopt_wrap(int sockfd, int level, int optname,
                         void *optval, socklen_t *optlen);

#undef setsockopt
#define setsockopt qemu_setsockopt_wrap
int qemu_setsockopt_wrap(int sockfd, int level, int optname,
                         const void *optval, socklen_t optlen);

#undef getpeername
#define getpeername qemu_getpeername_wrap
int qemu_getpeername_wrap(int sockfd, struct sockaddr *addr,
                          socklen_t *addrlen);

#undef getsockname
#define getsockname qemu_getsockname_wrap
int qemu_getsockname_wrap(int sockfd, struct sockaddr *addr,
                          socklen_t *addrlen);

#undef send
#define send qemu_send_wrap
ssize_t qemu_send_wrap(int sockfd, const void *buf, size_t len, int flags);

#undef sendto
#define sendto qemu_sendto_wrap
ssize_t qemu_sendto_wrap(int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr *addr, socklen_t addrlen);

#undef recv
#define recv qemu_recv_wrap
ssize_t qemu_recv_wrap(int sockfd, void *buf, size_t len, int flags);

#undef recvfrom
#define recvfrom qemu_recvfrom_wrap
ssize_t qemu_recvfrom_wrap(int sockfd, void *buf, size_t len, int flags,
                           struct sockaddr *addr, socklen_t *addrlen);

#endif
