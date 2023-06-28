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
#include "qemu/typedefs.h"

#ifdef HAVE_AFUNIX_H
#include <afunix.h>
#else
/*
 * Fallback definitions of things we need in afunix.h, if not available from
 * the used Windows SDK or MinGW headers.
 */
#define UNIX_PATH_MAX 108

typedef struct sockaddr_un {
    ADDRESS_FAMILY sun_family;
    char sun_path[UNIX_PATH_MAX];
} SOCKADDR_UN, *PSOCKADDR_UN;

#define SIO_AF_UNIX_GETPEERPID _WSAIOR(IOC_VENDOR, 256)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__)
/*
 * On windows-arm64, setjmp is available in only one variant, and longjmp always
 * does stack unwinding. This crash with generated code.
 * Thus, we use another implementation of setjmp (not windows one), coming from
 * mingw, which never performs stack unwinding.
 */
#undef setjmp
#undef longjmp
/*
 * These functions are not declared in setjmp.h because __aarch64__ defines
 * setjmp to _setjmpex instead. However, they are still defined in libmingwex.a,
 * which gets linked automatically.
 */
extern int __mingw_setjmp(jmp_buf);
extern void __attribute__((noreturn)) __mingw_longjmp(jmp_buf, int);
#define setjmp(env) __mingw_setjmp(env)
#define longjmp(env, val) __mingw_longjmp(env, val)
#elif defined(_WIN64)
/*
 * On windows-x64, setjmp is implemented by _setjmp which needs a second parameter.
 * If this parameter is NULL, longjump does no stack unwinding.
 * That is what we need for QEMU. Passing the value of register rsp (default)
 * lets longjmp try a stack unwinding which will crash with generated code.
 */
# undef setjmp
# define setjmp(env) _setjmp(env, NULL)
#endif /* __aarch64__ */
/* QEMU uses sigsetjmp()/siglongjmp() as the portable way to specify
 * "longjmp and don't touch the signal masks". Since we know that the
 * savemask parameter will always be zero we can safely define these
 * in terms of setjmp/longjmp on Win32.
 */
#define sigjmp_buf jmp_buf
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)

/* Missing POSIX functions. Don't use MinGW-w64 macros. */
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#undef gmtime_r
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#undef localtime_r
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif /* _POSIX_THREAD_SAFE_FUNCTIONS */

static inline void os_setup_signal_handling(void) {}
static inline void os_daemonize(void) {}
static inline void os_setup_post(void) {}
static inline void os_set_proc_name(const char *dummy) {}
static inline int os_parse_cmd_args(int index, const char *optarg) { return -1; }
void os_set_line_buffering(void);
void os_setup_early_signal_handling(void);

int getpagesize(void);

#if !defined(EPROTONOSUPPORT)
# define EPROTONOSUPPORT EINVAL
#endif

static inline int os_set_daemonize(bool d)
{
    if (d) {
        return -ENOTSUP;
    }
    return 0;
}

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

/*
 * Older versions of MinGW do not import _lock_file and _unlock_file properly.
 * This was fixed for v6.0.0 with commit b48e3ac8969d.
 */
static inline void qemu_flockfile(FILE *f)
{
#ifdef HAVE__LOCK_FILE
    _lock_file(f);
#endif
}

static inline void qemu_funlockfile(FILE *f)
{
#ifdef HAVE__LOCK_FILE
    _unlock_file(f);
#endif
}

/* Helper for WSAEventSelect, to report errors */
bool qemu_socket_select(int sockfd, WSAEVENT hEventObject,
                        long lNetworkEvents, Error **errp);

bool qemu_socket_unselect(int sockfd, Error **errp);

/* We wrap all the sockets functions so that we can set errno based on
 * WSAGetLastError(), and use file-descriptors instead of SOCKET.
 */

/*
 * qemu_close_socket_osfhandle:
 * @fd: a file descriptor associated with a SOCKET
 *
 * Close only the C run-time file descriptor, leave the SOCKET opened.
 *
 * Returns zero on success. On error, -1 is returned, and errno is set to
 * indicate the error.
 */
int qemu_close_socket_osfhandle(int fd);

#undef close
#define close qemu_close_wrap
int qemu_close_wrap(int fd);

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

EXCEPTION_DISPOSITION
win32_close_exception_handler(struct _EXCEPTION_RECORD*, void*,
                              struct _CONTEXT*, void*);

void *qemu_win32_map_alloc(size_t size, HANDLE *h, Error **errp);
void qemu_win32_map_free(void *ptr, HANDLE h, Error **errp);

#ifdef __cplusplus
}
#endif

#endif
