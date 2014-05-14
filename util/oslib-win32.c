/*
 * os-win32.c
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
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
#include <windows.h>
#include <glib.h>
#include <stdlib.h>
#include "config-host.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "trace.h"
#include "qemu/sockets.h"

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

void *qemu_memalign(size_t alignment, size_t size)
{
    void *ptr;

    if (!size) {
        abort();
    }
    ptr = qemu_oom_check(VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE));
    trace_qemu_memalign(alignment, size, ptr);
    return ptr;
}

void *qemu_anon_ram_alloc(size_t size)
{
    void *ptr;

    /* FIXME: this is not exactly optimal solution since VirtualAlloc
       has 64Kb granularity, but at least it guarantees us that the
       memory is page aligned. */
    ptr = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    trace_qemu_anon_ram_alloc(size, ptr);
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

void qemu_set_block(int fd)
{
    unsigned long opt = 0;
    WSAEventSelect(fd, NULL, 0);
    ioctlsocket(fd, FIONBIO, &opt);
}

void qemu_set_nonblock(int fd)
{
    unsigned long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);
    qemu_fd_register(fd);
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

static char exec_dir[PATH_MAX];

void qemu_init_exec_dir(const char *argv0)
{

    char *p;
    char buf[MAX_PATH];
    DWORD len;

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
        pstrcpy(exec_dir, sizeof(exec_dir), buf);
    }
}

char *qemu_get_exec_dir(void)
{
    return g_strdup(exec_dir);
}

/*
 * g_poll has a problem on Windows when using
 * timeouts < 10ms, in glib/gpoll.c:
 *
 * // If not, and we have a significant timeout, poll again with
 * // timeout then. Note that this will return indication for only
 * // one event, or only for messages. We ignore timeouts less than
 * // ten milliseconds as they are mostly pointless on Windows, the
 * // MsgWaitForMultipleObjectsEx() call will timeout right away
 * // anyway.
 *
 * if (retval == 0 && (timeout == INFINITE || timeout >= 10))
 *   retval = poll_rest (poll_msgs, handles, nhandles, fds, nfds, timeout);
 *
 * So whenever g_poll is called with timeout < 10ms it does
 * a quick poll instead of wait, this causes significant performance
 * degradation of QEMU, thus we should use WaitForMultipleObjectsEx
 * directly
 */
gint g_poll_fixed(GPollFD *fds, guint nfds, gint timeout)
{
    guint i;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    gint nhandles = 0;
    int num_completed = 0;

    for (i = 0; i < nfds; i++) {
        gint j;

        if (fds[i].fd <= 0) {
            continue;
        }

        /* don't add same handle several times
         */
        for (j = 0; j < nhandles; j++) {
            if (handles[j] == (HANDLE)fds[i].fd) {
                break;
            }
        }

        if (j == nhandles) {
            if (nhandles == MAXIMUM_WAIT_OBJECTS) {
                fprintf(stderr, "Too many handles to wait for!\n");
                break;
            } else {
                handles[nhandles++] = (HANDLE)fds[i].fd;
            }
        }
    }

    for (i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
    }

    if (timeout == -1) {
        timeout = INFINITE;
    }

    if (nhandles == 0) {
        if (timeout == INFINITE) {
            return -1;
        } else {
            SleepEx(timeout, TRUE);
            return 0;
        }
    }

    while (1) {
        DWORD res;
        gint j;

        res = WaitForMultipleObjectsEx(nhandles, handles, FALSE,
            timeout, TRUE);

        if (res == WAIT_FAILED) {
            for (i = 0; i < nfds; ++i) {
                fds[i].revents = 0;
            }

            return -1;
        } else if ((res == WAIT_TIMEOUT) || (res == WAIT_IO_COMPLETION) ||
                   ((int)res < (int)WAIT_OBJECT_0) ||
                   (res >= (WAIT_OBJECT_0 + nhandles))) {
            break;
        }

        for (i = 0; i < nfds; ++i) {
            if (handles[res - WAIT_OBJECT_0] == (HANDLE)fds[i].fd) {
                fds[i].revents = fds[i].events;
            }
        }

        ++num_completed;

        if (nhandles <= 1) {
            break;
        }

        /* poll the rest of the handles
         */
        for (j = res - WAIT_OBJECT_0 + 1; j < nhandles; j++) {
            handles[j - 1] = handles[j];
        }
        --nhandles;

        timeout = 0;
    }

    return num_completed;
}

size_t getpagesize(void)
{
    SYSTEM_INFO system_info;

    GetSystemInfo(&system_info);
    return system_info.dwPageSize;
}

void os_mem_prealloc(int fd, char *area, size_t memory)
{
    int i;
    size_t pagesize = getpagesize();

    memory = (memory + pagesize - 1) & -pagesize;
    for (i = 0; i < memory / pagesize; i++) {
        memset(area + pagesize * i, 0, 1);
    }
}
