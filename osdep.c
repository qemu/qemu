/*
 * QEMU low level functions
 *
 * Copyright (c) 2003 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef CONFIG_SOLARIS
#include <sys/types.h>
#include <sys/statvfs.h>
#endif

/* Needed early for CONFIG_BSD etc. */
#include "config-host.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(CONFIG_BSD)
#include <stdlib.h>
#else
#include <malloc.h>
#endif

#include "qemu-common.h"
#include "sysemu.h"
#include "qemu_socket.h"

#if !defined(_POSIX_C_SOURCE) || defined(_WIN32) || defined(__sun__)
static void *oom_check(void *ptr)
{
    if (ptr == NULL) {
        abort();
    }
    return ptr;
}
#endif

#if defined(_WIN32)
void *qemu_memalign(size_t alignment, size_t size)
{
    if (!size) {
        abort();
    }
    return oom_check(VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE));
}

void *qemu_vmalloc(size_t size)
{
    /* FIXME: this is not exactly optimal solution since VirtualAlloc
       has 64Kb granularity, but at least it guarantees us that the
       memory is page aligned. */
    if (!size) {
        abort();
    }
    return oom_check(VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE));
}

void qemu_vfree(void *ptr)
{
    VirtualFree(ptr, 0, MEM_RELEASE);
}

#else

void *qemu_memalign(size_t alignment, size_t size)
{
#if defined(_POSIX_C_SOURCE) && !defined(__sun__)
    int ret;
    void *ptr;
    ret = posix_memalign(&ptr, alignment, size);
    if (ret != 0)
        abort();
    return ptr;
#elif defined(CONFIG_BSD)
    return oom_check(valloc(size));
#else
    return oom_check(memalign(alignment, size));
#endif
}

/* alloc shared memory pages */
void *qemu_vmalloc(size_t size)
{
    return qemu_memalign(getpagesize(), size);
}

void qemu_vfree(void *ptr)
{
    free(ptr);
}

#endif

int qemu_create_pidfile(const char *filename)
{
    char buffer[128];
    int len;
#ifndef _WIN32
    int fd;

    fd = open(filename, O_RDWR | O_CREAT, 0600);
    if (fd == -1)
        return -1;

    if (lockf(fd, F_TLOCK, 0) == -1)
        return -1;

    len = snprintf(buffer, sizeof(buffer), "%ld\n", (long)getpid());
    if (write(fd, buffer, len) != len)
        return -1;
#else
    HANDLE file;
    DWORD flags;
    OVERLAPPED overlap;
    BOOL ret;

    /* Open for writing with no sharing. */
    file = CreateFile(filename, GENERIC_WRITE, 0, NULL,
		      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file == INVALID_HANDLE_VALUE)
      return -1;

    flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
    overlap.hEvent = 0;
    /* Lock 1 byte. */
    ret = LockFileEx(file, flags, 0, 0, 1, &overlap);
    if (ret == 0)
      return -1;

    /* Write PID to file. */
    len = snprintf(buffer, sizeof(buffer), "%ld\n", (long)getpid());
    ret = WriteFileEx(file, (LPCVOID)buffer, (DWORD)len,
		      &overlap, NULL);
    if (ret == 0)
      return -1;
#endif
    return 0;
}

#ifdef _WIN32

/* Offset between 1/1/1601 and 1/1/1970 in 100 nanosec units */
#define _W32_FT_OFFSET (116444736000000000ULL)

int qemu_gettimeofday(qemu_timeval *tp)
{
  union {
    unsigned long long ns100; /*time since 1 Jan 1601 in 100ns units */
    FILETIME ft;
  }  _now;

  if(tp)
    {
      GetSystemTimeAsFileTime (&_now.ft);
      tp->tv_usec=(long)((_now.ns100 / 10ULL) % 1000000ULL );
      tp->tv_sec= (long)((_now.ns100 - _W32_FT_OFFSET) / 10000000ULL);
    }
  /* Always return 0 as per Open Group Base Specifications Issue 6.
     Do not set errno on error.  */
  return 0;
}
#endif /* _WIN32 */


#ifdef _WIN32
void socket_set_nonblock(int fd)
{
    unsigned long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);
}

int inet_aton(const char *cp, struct in_addr *ia)
{
    uint32_t addr = inet_addr(cp);
    if (addr == 0xffffffff)
	return 0;
    ia->s_addr = addr;
    return 1;
}
#else
void socket_set_nonblock(int fd)
{
    int f;
    f = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, f | O_NONBLOCK);
}
#endif
