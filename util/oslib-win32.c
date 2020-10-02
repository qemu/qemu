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
 *
 * The implementation of g_poll (functions poll_rest, g_poll) at the end of
 * this file are based on code from GNOME glib-2 and use a different license,
 * see the license comment there.
 */

#include "qemu/osdep.h"
#include <windows.h>
#include "qemu-common.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "trace.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"

/* this must come after including "trace.h" */
#include <shlobj.h>

void *qemu_oom_check(void *ptr)
{
    if (ptr == NULL) {
        fprintf(stderr, "Failed to allocate memory: %lu\n", GetLastError());
        abort();
    }
    return ptr;
}

void *qemu_try_memalign(size_t alignment, size_t size)
{
    void *ptr;

    if (!size) {
        abort();
    }
    ptr = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    trace_qemu_memalign(alignment, size, ptr);
    return ptr;
}

void *qemu_memalign(size_t alignment, size_t size)
{
    return qemu_oom_check(qemu_try_memalign(alignment, size));
}

static int get_allocation_granularity(void)
{
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    return system_info.dwAllocationGranularity;
}

void *qemu_anon_ram_alloc(size_t size, uint64_t *align, bool shared)
{
    void *ptr;

    ptr = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    trace_qemu_anon_ram_alloc(size, ptr);

    if (ptr && align) {
        *align = MAX(get_allocation_granularity(), getpagesize());
    }
    return ptr;
}

void qemu_vfree(void *ptr)
{
    trace_qemu_vfree(ptr);
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

void qemu_anon_ram_free(void *ptr, size_t size)
{
    trace_qemu_anon_ram_free(ptr, size);
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

#ifndef CONFIG_LOCALTIME_R
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
#endif /* CONFIG_LOCALTIME_R */

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

void qemu_set_block(int fd)
{
    unsigned long opt = 0;
    WSAEventSelect(fd, NULL, 0);
    ioctlsocket(fd, FIONBIO, &opt);
}

int qemu_try_set_nonblock(int fd)
{
    unsigned long opt = 1;
    if (ioctlsocket(fd, FIONBIO, &opt) != NO_ERROR) {
        return -socket_error();
    }
    qemu_fd_register(fd);
    return 0;
}

void qemu_set_nonblock(int fd)
{
    (void)qemu_try_set_nonblock(fd);
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

/* Offset between 1/1/1601 and 1/1/1970 in 100 nanosec units */
#define _W32_FT_OFFSET (116444736000000000ULL)

int qemu_gettimeofday(qemu_timeval *tp)
{
  union {
    unsigned long long ns100; /*time since 1 Jan 1601 in 100ns units */
    FILETIME ft;
  }  _now;

  if(tp) {
      GetSystemTimeAsFileTime (&_now.ft);
      tp->tv_usec=(long)((_now.ns100 / 10ULL) % 1000000ULL );
      tp->tv_sec= (long)((_now.ns100 - _W32_FT_OFFSET) / 10000000ULL);
  }
  /* Always return 0 as per Open Group Base Specifications Issue 6.
     Do not set errno on error.  */
  return 0;
}

int qemu_get_thread_id(void)
{
    return GetCurrentThreadId();
}

char *
qemu_get_local_state_pathname(const char *relative_pathname)
{
    HRESULT result;
    char base_path[MAX_PATH+1] = "";

    result = SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL,
                             /* SHGFP_TYPE_CURRENT */ 0, base_path);
    if (result != S_OK) {
        /* misconfigured environment */
        g_critical("CSIDL_COMMON_APPDATA unavailable: %ld", (long)result);
        abort();
    }
    return g_strdup_printf("%s" G_DIR_SEPARATOR_S "%s", base_path,
                           relative_pathname);
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

static const char *exec_dir;

void qemu_init_exec_dir(const char *argv0)
{

    char *p;
    char buf[MAX_PATH];
    DWORD len;

    if (exec_dir) {
        return;
    }

    len = GetModuleFileName(NULL, buf, sizeof(buf) - 1);
    if (len == 0) {
        return;
    }

    buf[len] = 0;
    p = buf + len - 1;
    while (p != buf && *p != '\\') {
        p--;
    }
    *p = 0;
    if (access(buf, R_OK) == 0) {
        exec_dir = g_strdup(buf);
    } else {
        exec_dir = CONFIG_BINDIR;
    }
}

const char *qemu_get_exec_dir(void)
{
    return exec_dir;
}

#if !GLIB_CHECK_VERSION(2, 50, 0)
/*
 * The original implementation of g_poll from glib has a problem on Windows
 * when using timeouts < 10 ms.
 *
 * Whenever g_poll is called with timeout < 10 ms, it does a quick poll instead
 * of wait. This causes significant performance degradation of QEMU.
 *
 * The following code is a copy of the original code from glib/gpoll.c
 * (glib commit 20f4d1820b8d4d0fc4447188e33efffd6d4a88d8 from 2014-02-19).
 * Some debug code was removed and the code was reformatted.
 * All other code modifications are marked with 'QEMU'.
 */

/*
 * gpoll.c: poll(2) abstraction
 * Copyright 1998 Owen Taylor
 * Copyright 2008 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

static int poll_rest(gboolean poll_msgs, HANDLE *handles, gint nhandles,
                     GPollFD *fds, guint nfds, gint timeout)
{
    DWORD ready;
    GPollFD *f;
    int recursed_result;

    if (poll_msgs) {
        /* Wait for either messages or handles
         * -> Use MsgWaitForMultipleObjectsEx
         */
        ready = MsgWaitForMultipleObjectsEx(nhandles, handles, timeout,
                                            QS_ALLINPUT, MWMO_ALERTABLE);

        if (ready == WAIT_FAILED) {
            gchar *emsg = g_win32_error_message(GetLastError());
            g_warning("MsgWaitForMultipleObjectsEx failed: %s", emsg);
            g_free(emsg);
        }
    } else if (nhandles == 0) {
        /* No handles to wait for, just the timeout */
        if (timeout == INFINITE) {
            ready = WAIT_FAILED;
        } else {
            SleepEx(timeout, TRUE);
            ready = WAIT_TIMEOUT;
        }
    } else {
        /* Wait for just handles
         * -> Use WaitForMultipleObjectsEx
         */
        ready =
            WaitForMultipleObjectsEx(nhandles, handles, FALSE, timeout, TRUE);
        if (ready == WAIT_FAILED) {
            gchar *emsg = g_win32_error_message(GetLastError());
            g_warning("WaitForMultipleObjectsEx failed: %s", emsg);
            g_free(emsg);
        }
    }

    if (ready == WAIT_FAILED) {
        return -1;
    } else if (ready == WAIT_TIMEOUT || ready == WAIT_IO_COMPLETION) {
        return 0;
    } else if (poll_msgs && ready == WAIT_OBJECT_0 + nhandles) {
        for (f = fds; f < &fds[nfds]; ++f) {
            if (f->fd == G_WIN32_MSG_HANDLE && f->events & G_IO_IN) {
                f->revents |= G_IO_IN;
            }
        }

        /* If we have a timeout, or no handles to poll, be satisfied
         * with just noticing we have messages waiting.
         */
        if (timeout != 0 || nhandles == 0) {
            return 1;
        }

        /* If no timeout and handles to poll, recurse to poll them,
         * too.
         */
        recursed_result = poll_rest(FALSE, handles, nhandles, fds, nfds, 0);
        return (recursed_result == -1) ? -1 : 1 + recursed_result;
    } else if (/* QEMU: removed the following unneeded statement which causes
                * a compiler warning: ready >= WAIT_OBJECT_0 && */
               ready < WAIT_OBJECT_0 + nhandles) {
        for (f = fds; f < &fds[nfds]; ++f) {
            if ((HANDLE) f->fd == handles[ready - WAIT_OBJECT_0]) {
                f->revents = f->events;
            }
        }

        /* If no timeout and polling several handles, recurse to poll
         * the rest of them.
         */
        if (timeout == 0 && nhandles > 1) {
            /* Remove the handle that fired */
            int i;
            for (i = ready - WAIT_OBJECT_0 + 1; i < nhandles; i++) {
                handles[i-1] = handles[i];
            }
            nhandles--;
            recursed_result = poll_rest(FALSE, handles, nhandles, fds, nfds, 0);
            return (recursed_result == -1) ? -1 : 1 + recursed_result;
        }
        return 1;
    }

    return 0;
}

gint g_poll(GPollFD *fds, guint nfds, gint timeout)
{
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    gboolean poll_msgs = FALSE;
    GPollFD *f;
    gint nhandles = 0;
    int retval;

    for (f = fds; f < &fds[nfds]; ++f) {
        if (f->fd == G_WIN32_MSG_HANDLE && (f->events & G_IO_IN)) {
            poll_msgs = TRUE;
        } else if (f->fd > 0) {
            /* Don't add the same handle several times into the array, as
             * docs say that is not allowed, even if it actually does seem
             * to work.
             */
            gint i;

            for (i = 0; i < nhandles; i++) {
                if (handles[i] == (HANDLE) f->fd) {
                    break;
                }
            }

            if (i == nhandles) {
                if (nhandles == MAXIMUM_WAIT_OBJECTS) {
                    g_warning("Too many handles to wait for!\n");
                    break;
                } else {
                    handles[nhandles++] = (HANDLE) f->fd;
                }
            }
        }
    }

    for (f = fds; f < &fds[nfds]; ++f) {
        f->revents = 0;
    }

    if (timeout == -1) {
        timeout = INFINITE;
    }

    /* Polling for several things? */
    if (nhandles > 1 || (nhandles > 0 && poll_msgs)) {
        /* First check if one or several of them are immediately
         * available
         */
        retval = poll_rest(poll_msgs, handles, nhandles, fds, nfds, 0);

        /* If not, and we have a significant timeout, poll again with
         * timeout then. Note that this will return indication for only
         * one event, or only for messages. We ignore timeouts less than
         * ten milliseconds as they are mostly pointless on Windows, the
         * MsgWaitForMultipleObjectsEx() call will timeout right away
         * anyway.
         *
         * Modification for QEMU: replaced timeout >= 10 by timeout > 0.
         */
        if (retval == 0 && (timeout == INFINITE || timeout > 0)) {
            retval = poll_rest(poll_msgs, handles, nhandles,
                               fds, nfds, timeout);
        }
    } else {
        /* Just polling for one thing, so no need to check first if
         * available immediately
         */
        retval = poll_rest(poll_msgs, handles, nhandles, fds, nfds, timeout);
    }

    if (retval == -1) {
        for (f = fds; f < &fds[nfds]; ++f) {
            f->revents = 0;
        }
    }

    return retval;
}
#endif

int getpagesize(void)
{
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}

void os_mem_prealloc(int fd, char *area, size_t memory, int smp_cpus,
                     Error **errp)
{
    int i;
    size_t pagesize = qemu_real_host_page_size;

    memory = (memory + pagesize - 1) & -pagesize;
    for (i = 0; i < memory / pagesize; i++) {
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

char *qemu_get_host_name(Error **errp)
{
    wchar_t tmp[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = G_N_ELEMENTS(tmp);

    if (GetComputerNameW(tmp, &size) == 0) {
        error_setg_win32(errp, GetLastError(), "failed close handle");
        return NULL;
    }

    return g_utf16_to_utf8(tmp, size, NULL, NULL, NULL);
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
