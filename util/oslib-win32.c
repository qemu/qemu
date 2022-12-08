/*
 * os-win32.c
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

#include "qemu/osdep.h"
#include <windows.h>
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "trace.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include <malloc.h>

static int get_allocation_granularity(void)
{
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    return system_info.dwAllocationGranularity;
}

void *qemu_anon_ram_alloc(size_t size, uint64_t *align, bool shared,
                          bool noreserve)
{
    void *ptr;

    if (noreserve) {
        /*
         * We need a MEM_COMMIT before accessing any memory in a MEM_RESERVE
         * area; we cannot easily mimic POSIX MAP_NORESERVE semantics.
         */
        error_report("Skipping reservation of swap space is not supported.");
        return NULL;
    }

    ptr = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    trace_qemu_anon_ram_alloc(size, ptr);

    if (ptr && align) {
        *align = MAX(get_allocation_granularity(), getpagesize());
    }
    return ptr;
}

void qemu_anon_ram_free(void *ptr, size_t size)
{
    trace_qemu_anon_ram_free(ptr, size);
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
/* FIXME: add proper locking */
struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    struct tm *p = gmtime(timep);
    memset(result, 0, sizeof(*result));
    if (p) {
        *result = *p;
        p = result;
    }
    return p;
}

/* FIXME: add proper locking */
struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    struct tm *p = localtime(timep);
    memset(result, 0, sizeof(*result));
    if (p) {
        *result = *p;
        p = result;
    }
    return p;
}
#endif /* _POSIX_THREAD_SAFE_FUNCTIONS */

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

void qemu_socket_set_block(int fd)
{
    unsigned long opt = 0;
    WSAEventSelect(fd, NULL, 0);
    ioctlsocket(fd, FIONBIO, &opt);
}

int qemu_socket_try_set_nonblock(int fd)
{
    unsigned long opt = 1;
    if (ioctlsocket(fd, FIONBIO, &opt) != NO_ERROR) {
        return -socket_error();
    }
    return 0;
}

void qemu_socket_set_nonblock(int fd)
{
    (void)qemu_socket_try_set_nonblock(fd);
}

int socket_set_fast_reuse(int fd)
{
    /* Enabling the reuse of an endpoint that was used by a socket still in
     * TIME_WAIT state is usually performed by setting SO_REUSEADDR. On Windows
     * fast reuse is the default and SO_REUSEADDR does strange things. So we
     * don't have to do anything here. More info can be found at:
     * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740621.aspx */
    return 0;
}

int inet_aton(const char *cp, struct in_addr *ia)
{
    uint32_t addr = inet_addr(cp);
    if (addr == 0xffffffff) {
        return 0;
    }
    ia->s_addr = addr;
    return 1;
}

void qemu_set_cloexec(int fd)
{
}

int qemu_get_thread_id(void)
{
    return GetCurrentThreadId();
}

char *
qemu_get_local_state_dir(void)
{
    const char * const *data_dirs = g_get_system_data_dirs();

    g_assert(data_dirs && data_dirs[0]);

    return g_strdup(data_dirs[0]);
}

void qemu_set_tty_echo(int fd, bool echo)
{
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    DWORD dwMode = 0;

    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    GetConsoleMode(handle, &dwMode);

    if (echo) {
        SetConsoleMode(handle, dwMode | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    } else {
        SetConsoleMode(handle,
                       dwMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
    }
}

int getpagesize(void)
{
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}

void qemu_prealloc_mem(int fd, char *area, size_t sz, int max_threads,
                       ThreadContext *tc, Error **errp)
{
    int i;
    size_t pagesize = qemu_real_host_page_size();

    sz = (sz + pagesize - 1) & -pagesize;
    for (i = 0; i < sz / pagesize; i++) {
        memset(area + pagesize * i, 0, 1);
    }
}

char *qemu_get_pid_name(pid_t pid)
{
    /* XXX Implement me */
    abort();
}


pid_t qemu_fork(Error **errp)
{
    errno = ENOSYS;
    error_setg_errno(errp, errno,
                     "cannot fork child process");
    return -1;
}


#undef connect
int qemu_connect_wrap(int sockfd, const struct sockaddr *addr,
                      socklen_t addrlen)
{
    int ret;
    ret = connect(sockfd, addr, addrlen);
    if (ret < 0) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            errno = EINPROGRESS;
        } else {
            errno = socket_error();
        }
    }
    return ret;
}


#undef listen
int qemu_listen_wrap(int sockfd, int backlog)
{
    int ret;
    ret = listen(sockfd, backlog);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef bind
int qemu_bind_wrap(int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen)
{
    int ret;
    ret = bind(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef socket
int qemu_socket_wrap(int domain, int type, int protocol)
{
    int ret;
    ret = socket(domain, type, protocol);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef accept
int qemu_accept_wrap(int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen)
{
    int ret;
    ret = accept(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef shutdown
int qemu_shutdown_wrap(int sockfd, int how)
{
    int ret;
    ret = shutdown(sockfd, how);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef ioctlsocket
int qemu_ioctlsocket_wrap(int fd, int req, void *val)
{
    int ret;
    ret = ioctlsocket(fd, req, val);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef closesocket
int qemu_closesocket_wrap(int fd)
{
    int ret;
    ret = closesocket(fd);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef getsockopt
int qemu_getsockopt_wrap(int sockfd, int level, int optname,
                         void *optval, socklen_t *optlen)
{
    int ret;
    ret = getsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef setsockopt
int qemu_setsockopt_wrap(int sockfd, int level, int optname,
                         const void *optval, socklen_t optlen)
{
    int ret;
    ret = setsockopt(sockfd, level, optname, optval, optlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef getpeername
int qemu_getpeername_wrap(int sockfd, struct sockaddr *addr,
                          socklen_t *addrlen)
{
    int ret;
    ret = getpeername(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef getsockname
int qemu_getsockname_wrap(int sockfd, struct sockaddr *addr,
                          socklen_t *addrlen)
{
    int ret;
    ret = getsockname(sockfd, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef send
ssize_t qemu_send_wrap(int sockfd, const void *buf, size_t len, int flags)
{
    int ret;
    ret = send(sockfd, buf, len, flags);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef sendto
ssize_t qemu_sendto_wrap(int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr *addr, socklen_t addrlen)
{
    int ret;
    ret = sendto(sockfd, buf, len, flags, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef recv
ssize_t qemu_recv_wrap(int sockfd, void *buf, size_t len, int flags)
{
    int ret;
    ret = recv(sockfd, buf, len, flags);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef recvfrom
ssize_t qemu_recvfrom_wrap(int sockfd, void *buf, size_t len, int flags,
                           struct sockaddr *addr, socklen_t *addrlen)
{
    int ret;
    ret = recvfrom(sockfd, buf, len, flags, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

bool qemu_write_pidfile(const char *filename, Error **errp)
{
    char buffer[128];
    int len;
    HANDLE file;
    OVERLAPPED overlap;
    BOOL ret;
    memset(&overlap, 0, sizeof(overlap));

    file = CreateFile(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed to create PID file");
        return false;
    }
    len = snprintf(buffer, sizeof(buffer), FMT_pid "\n", (pid_t)getpid());
    ret = WriteFile(file, (LPCVOID)buffer, (DWORD)len,
                    NULL, &overlap);
    CloseHandle(file);
    if (ret == 0) {
        error_setg(errp, "Failed to write PID file");
        return false;
    }
    return true;
}

size_t qemu_get_host_physmem(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    if (GlobalMemoryStatusEx(&statex)) {
        return statex.ullTotalPhys;
    }
    return 0;
}

int qemu_msync(void *addr, size_t length, int fd)
{
    /**
     * Perform the sync based on the file descriptor
     * The sync range will most probably be wider than the one
     * requested - but it will still get the job done
     */
    return qemu_fdatasync(fd);
}
