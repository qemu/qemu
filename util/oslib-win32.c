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
    qemu_socket_unselect(fd, NULL);
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

bool qemu_prealloc_mem(int fd, char *area, size_t sz, int max_threads,
                       ThreadContext *tc, bool async, Error **errp)
{
    int i;
    size_t pagesize = qemu_real_host_page_size();

    sz = (sz + pagesize - 1) & -pagesize;
    for (i = 0; i < sz / pagesize; i++) {
        memset(area + pagesize * i, 0, 1);
    }

    return true;
}

bool qemu_finish_async_prealloc_mem(Error **errp)
{
    /* async prealloc not supported, there is nothing to finish */
    return true;
}

char *qemu_get_pid_name(pid_t pid)
{
    /* XXX Implement me */
    abort();
}


bool qemu_socket_select(int sockfd, WSAEVENT hEventObject,
                        long lNetworkEvents, Error **errp)
{
    SOCKET s = _get_osfhandle(sockfd);

    if (errp == NULL) {
        errp = &error_warn;
    }

    if (s == INVALID_SOCKET) {
        error_setg(errp, "invalid socket fd=%d", sockfd);
        return false;
    }

    if (WSAEventSelect(s, hEventObject, lNetworkEvents) != 0) {
        error_setg_win32(errp, WSAGetLastError(), "failed to WSAEventSelect()");
        return false;
    }

    return true;
}

bool qemu_socket_unselect(int sockfd, Error **errp)
{
    return qemu_socket_select(sockfd, NULL, 0, errp);
}

int qemu_socketpair(int domain, int type, int protocol, int sv[2])
{
    struct sockaddr_un addr = {
        0,
    };
    socklen_t socklen;
    int listener = -1;
    int client = -1;
    int server = -1;
    g_autofree char *path = NULL;
    int tmpfd;
    u_long arg;
    int ret = -1;

    g_return_val_if_fail(sv != NULL, -1);

    addr.sun_family = AF_UNIX;
    socklen = sizeof(addr);

    tmpfd = g_file_open_tmp(NULL, &path, NULL);
    if (tmpfd == -1 || !path) {
        errno = EACCES;
        goto out;
    }

    close(tmpfd);

    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = EINVAL;
        goto out;
    }

    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    listener = socket(domain, type, protocol);
    if (listener == -1) {
        goto out;
    }

    if (DeleteFile(path) == 0 && GetLastError() != ERROR_FILE_NOT_FOUND) {
        errno = EACCES;
        goto out;
    }
    g_clear_pointer(&path, g_free);

    if (bind(listener, (struct sockaddr *)&addr, socklen) == -1) {
        goto out;
    }

    if (listen(listener, 1) == -1) {
        goto out;
    }

    client = socket(domain, type, protocol);
    if (client == -1) {
        goto out;
    }

    arg = 1;
    if (ioctlsocket(client, FIONBIO, &arg) != NO_ERROR) {
        goto out;
    }

    if (connect(client, (struct sockaddr *)&addr, socklen) == -1 &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        goto out;
    }

    server = accept(listener, NULL, NULL);
    if (server == -1) {
        goto out;
    }

    arg = 0;
    if (ioctlsocket(client, FIONBIO, &arg) != NO_ERROR) {
        goto out;
    }

    arg = 0;
    if (ioctlsocket(client, SIO_AF_UNIX_GETPEERPID, &arg) != NO_ERROR) {
        goto out;
    }

    if (arg != GetCurrentProcessId()) {
        errno = EPERM;
        goto out;
    }

    sv[0] = server;
    server = -1;
    sv[1] = client;
    client = -1;
    ret = 0;

out:
    if (listener != -1) {
        close(listener);
    }
    if (client != -1) {
        close(client);
    }
    if (server != -1) {
        close(server);
    }
    if (path) {
        DeleteFile(path);
    }
    return ret;
}

#undef connect
int qemu_connect_wrap(int sockfd, const struct sockaddr *addr,
                      socklen_t addrlen)
{
    int ret;
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = connect(s, addr, addrlen);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = listen(s, backlog);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = bind(s, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}

QEMU_USED EXCEPTION_DISPOSITION
win32_close_exception_handler(struct _EXCEPTION_RECORD *exception_record,
                              void *registration, struct _CONTEXT *context,
                              void *dispatcher)
{
    return EXCEPTION_EXECUTE_HANDLER;
}

#undef close
int qemu_close_socket_osfhandle(int fd)
{
    SOCKET s = _get_osfhandle(fd);
    DWORD flags = 0;

    /*
     * If we were to just call _close on the descriptor, it would close the
     * HANDLE, but it wouldn't free any of the resources associated to the
     * SOCKET, and we can't call _close after calling closesocket, because
     * closesocket has already closed the HANDLE, and _close would attempt to
     * close the HANDLE again, resulting in a double free. We can however
     * protect the HANDLE from actually being closed long enough to close the
     * file descriptor, then close the socket itself.
     */
    if (!GetHandleInformation((HANDLE)s, &flags)) {
        errno = EACCES;
        return -1;
    }

    if (!SetHandleInformation((HANDLE)s, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE)) {
        errno = EACCES;
        return -1;
    }

    __try1(win32_close_exception_handler) {
        /*
         * close() returns EBADF since we PROTECT_FROM_CLOSE the underlying
         * handle, but the FD is actually freed
         */
        if (close(fd) < 0 && errno != EBADF) {
            return -1;
        }
    }
    __except1 {
    }

    if (!SetHandleInformation((HANDLE)s, flags, flags)) {
        errno = EACCES;
        return -1;
    }

    return 0;
}

int qemu_close_wrap(int fd)
{
    SOCKET s = INVALID_SOCKET;
    int ret = -1;

    if (!fd_is_socket(fd)) {
        return close(fd);
    }

    s = _get_osfhandle(fd);
    qemu_close_socket_osfhandle(fd);

    ret = closesocket(s);
    if (ret < 0) {
        errno = socket_error();
    }

    return ret;
}


#undef socket
int qemu_socket_wrap(int domain, int type, int protocol)
{
    SOCKET s;
    int fd;

    s = socket(domain, type, protocol);
    if (s == -1) {
        errno = socket_error();
        return -1;
    }

    fd = _open_osfhandle(s, _O_BINARY);
    if (fd < 0) {
        closesocket(s);
        /* _open_osfhandle may not set errno, and closesocket() may override it */
        errno = ENOMEM;
    }

    return fd;
}


#undef accept
int qemu_accept_wrap(int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen)
{
    int fd;
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    s = accept(s, addr, addrlen);
    if (s == -1) {
        errno = socket_error();
        return -1;
    }

    fd = _open_osfhandle(s, _O_BINARY);
    if (fd < 0) {
        closesocket(s);
        /* _open_osfhandle may not set errno, and closesocket() may override it */
        errno = ENOMEM;
    }

    return fd;
}


#undef shutdown
int qemu_shutdown_wrap(int sockfd, int how)
{
    int ret;
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = shutdown(s, how);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef ioctlsocket
int qemu_ioctlsocket_wrap(int fd, int req, void *val)
{
    int ret;
    SOCKET s = _get_osfhandle(fd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = ioctlsocket(s, req, val);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = getsockopt(s, level, optname, optval, optlen);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = setsockopt(s, level, optname, optval, optlen);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = getpeername(s, addr, addrlen);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = getsockname(s, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef send
ssize_t qemu_send_wrap(int sockfd, const void *buf, size_t len, int flags)
{
    int ret;
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = send(s, buf, len, flags);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = sendto(s, buf, len, flags, addr, addrlen);
    if (ret < 0) {
        errno = socket_error();
    }
    return ret;
}


#undef recv
ssize_t qemu_recv_wrap(int sockfd, void *buf, size_t len, int flags)
{
    int ret;
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = recv(s, buf, len, flags);
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
    SOCKET s = _get_osfhandle(sockfd);

    if (s == INVALID_SOCKET) {
        return -1;
    }

    ret = recvfrom(s, buf, len, flags, addr, addrlen);
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

void *qemu_win32_map_alloc(size_t size, HANDLE *h, Error **errp)
{
    void *bits;

    trace_win32_map_alloc(size);

    *h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                          size, NULL);
    if (*h == NULL) {
        error_setg_win32(errp, GetLastError(), "Failed to CreateFileMapping");
        return NULL;
    }

    bits = MapViewOfFile(*h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (bits == NULL) {
        error_setg_win32(errp, GetLastError(), "Failed to MapViewOfFile");
        CloseHandle(*h);
        return NULL;
    }

    return bits;
}

void qemu_win32_map_free(void *ptr, HANDLE h, Error **errp)
{
    trace_win32_map_free(ptr, h);

    if (UnmapViewOfFile(ptr) == 0) {
        error_setg_win32(errp, GetLastError(), "Failed to UnmapViewOfFile");
    }
    CloseHandle(h);
}
