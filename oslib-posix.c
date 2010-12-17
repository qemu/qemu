/*
 * os-posix-lib.c
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * QEMU library functions on POSIX which are shared between QEMU and
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

#include "config-host.h"
#include "sysemu.h"
#include "trace.h"
#include "qemu_socket.h"

void *qemu_oom_check(void *ptr)
{
    if (ptr == NULL) {
        fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
        abort();
    }
    return ptr;
}

void *qemu_memalign(size_t alignment, size_t size)
{
    void *ptr;
#if defined(_POSIX_C_SOURCE) && !defined(__sun__)
    int ret;
    ret = posix_memalign(&ptr, alignment, size);
    if (ret != 0) {
        fprintf(stderr, "Failed to allocate %zu B: %s\n",
                size, strerror(ret));
        abort();
    }
#elif defined(CONFIG_BSD)
    ptr = qemu_oom_check(valloc(size));
#else
    ptr = qemu_oom_check(memalign(alignment, size));
#endif
    trace_qemu_memalign(alignment, size, ptr);
    return ptr;
}

/* alloc shared memory pages */
void *qemu_vmalloc(size_t size)
{
    return qemu_memalign(getpagesize(), size);
}

void qemu_vfree(void *ptr)
{
    trace_qemu_vfree(ptr);
    free(ptr);
}

void socket_set_nonblock(int fd)
{
    int f;
    f = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

void qemu_set_cloexec(int fd)
{
    int f;
    f = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

/*
 * Creates a pipe with FD_CLOEXEC set on both file descriptors
 */
int qemu_pipe(int pipefd[2])
{
    int ret;

#ifdef CONFIG_PIPE2
    ret = pipe2(pipefd, O_CLOEXEC);
    if (ret != -1 || errno != ENOSYS) {
        return ret;
    }
#endif
    ret = pipe(pipefd);
    if (ret == 0) {
        qemu_set_cloexec(pipefd[0]);
        qemu_set_cloexec(pipefd[1]);
    }

    return ret;
}

int qemu_utimensat(int dirfd, const char *path, const struct timespec *times,
                   int flags)
{
    struct timeval tv[2], tv_now;
    struct stat st;
    int i;
#ifdef CONFIG_UTIMENSAT
    int ret;

    ret = utimensat(dirfd, path, times, flags);
    if (ret != -1 || errno != ENOSYS) {
        return ret;
    }
#endif
    /* Fallback: use utimes() instead of utimensat() */

    /* happy if special cases */
    if (times[0].tv_nsec == UTIME_OMIT && times[1].tv_nsec == UTIME_OMIT) {
        return 0;
    }
    if (times[0].tv_nsec == UTIME_NOW && times[1].tv_nsec == UTIME_NOW) {
        return utimes(path, NULL);
    }

    /* prepare for hard cases */
    if (times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW) {
        gettimeofday(&tv_now, NULL);
    }
    if (times[0].tv_nsec == UTIME_OMIT || times[1].tv_nsec == UTIME_OMIT) {
        stat(path, &st);
    }

    for (i = 0; i < 2; i++) {
        if (times[i].tv_nsec == UTIME_NOW) {
            tv[i].tv_sec = tv_now.tv_sec;
            tv[i].tv_usec = tv_now.tv_usec;
        } else if (times[i].tv_nsec == UTIME_OMIT) {
            tv[i].tv_sec = (i == 0) ? st.st_atime : st.st_mtime;
            tv[i].tv_usec = 0;
        } else {
            tv[i].tv_sec = times[i].tv_sec;
            tv[i].tv_usec = times[i].tv_nsec / 1000;
        }
    }

    return utimes(path, &tv[0]);
}
