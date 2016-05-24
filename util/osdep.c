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
#include "qemu/osdep.h"

/* Needed early for CONFIG_BSD etc. */

#if defined(CONFIG_MADVISE) || defined(CONFIG_POSIX_MADVISE)
#include <sys/mman.h>
#endif

#ifdef CONFIG_SOLARIS
#include <sys/statvfs.h>
/* See MySQL bug #7156 (http://bugs.mysql.com/bug.php?id=7156) for
   discussion about Solaris header problems */
extern int madvise(caddr_t, size_t, int);
#endif

#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "monitor/monitor.h"

static bool fips_enabled = false;

static const char *hw_version = QEMU_HW_VERSION;

int socket_set_cork(int fd, int v)
{
#if defined(SOL_TCP) && defined(TCP_CORK)
    return qemu_setsockopt(fd, SOL_TCP, TCP_CORK, &v, sizeof(v));
#else
    return 0;
#endif
}

int socket_set_nodelay(int fd)
{
    int v = 1;
    return qemu_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
}

int qemu_madvise(void *addr, size_t len, int advice)
{
    if (advice == QEMU_MADV_INVALID) {
        errno = EINVAL;
        return -1;
    }
#if defined(CONFIG_MADVISE)
    return madvise(addr, len, advice);
#elif defined(CONFIG_POSIX_MADVISE)
    return posix_madvise(addr, len, advice);
#else
    errno = EINVAL;
    return -1;
#endif
}

#ifndef _WIN32
/*
 * Dups an fd and sets the flags
 */
static int qemu_dup_flags(int fd, int flags)
{
    int ret;
    int serrno;
    int dup_flags;

#ifdef F_DUPFD_CLOEXEC
    ret = fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
    ret = dup(fd);
    if (ret != -1) {
        qemu_set_cloexec(ret);
    }
#endif
    if (ret == -1) {
        goto fail;
    }

    dup_flags = fcntl(ret, F_GETFL);
    if (dup_flags == -1) {
        goto fail;
    }

    if ((flags & O_SYNC) != (dup_flags & O_SYNC)) {
        errno = EINVAL;
        goto fail;
    }

    /* Set/unset flags that we can with fcntl */
    if (fcntl(ret, F_SETFL, flags) == -1) {
        goto fail;
    }

    /* Truncate the file in the cases that open() would truncate it */
    if (flags & O_TRUNC ||
            ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))) {
        if (ftruncate(ret, 0) == -1) {
            goto fail;
        }
    }

    return ret;

fail:
    serrno = errno;
    if (ret != -1) {
        close(ret);
    }
    errno = serrno;
    return -1;
}

static int qemu_parse_fdset(const char *param)
{
    return qemu_parse_fd(param);
}
#endif

/*
 * Opens a file with FD_CLOEXEC set
 */
int qemu_open(const char *name, int flags, ...)
{
    int ret;
    int mode = 0;

#ifndef _WIN32
    const char *fdset_id_str;

    /* Attempt dup of fd from fd set */
    if (strstart(name, "/dev/fdset/", &fdset_id_str)) {
        int64_t fdset_id;
        int fd, dupfd;

        fdset_id = qemu_parse_fdset(fdset_id_str);
        if (fdset_id == -1) {
            errno = EINVAL;
            return -1;
        }

        fd = monitor_fdset_get_fd(fdset_id, flags);
        if (fd == -1) {
            return -1;
        }

        dupfd = qemu_dup_flags(fd, flags);
        if (dupfd == -1) {
            return -1;
        }

        ret = monitor_fdset_dup_fd_add(fdset_id, dupfd);
        if (ret == -1) {
            close(dupfd);
            errno = EINVAL;
            return -1;
        }

        return dupfd;
    }
#endif

    if (flags & O_CREAT) {
        va_list ap;

        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }

#ifdef O_CLOEXEC
    ret = open(name, flags | O_CLOEXEC, mode);
#else
    ret = open(name, flags, mode);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }
#endif

#ifdef O_DIRECT
    if (ret == -1 && errno == EINVAL && (flags & O_DIRECT)) {
        error_report("file system may not support O_DIRECT");
        errno = EINVAL; /* in case it was clobbered */
    }
#endif /* O_DIRECT */

    return ret;
}

int qemu_close(int fd)
{
    int64_t fdset_id;

    /* Close fd that was dup'd from an fdset */
    fdset_id = monitor_fdset_dup_fd_find(fd);
    if (fdset_id != -1) {
        int ret;

        ret = close(fd);
        if (ret == 0) {
            monitor_fdset_dup_fd_remove(fd);
        }

        return ret;
    }

    return close(fd);
}

/*
 * A variant of write(2) which handles partial write.
 *
 * Return the number of bytes transferred.
 * Set errno if fewer than `count' bytes are written.
 *
 * This function don't work with non-blocking fd's.
 * Any of the possibilities with non-bloking fd's is bad:
 *   - return a short write (then name is wrong)
 *   - busy wait adding (errno == EAGAIN) to the loop
 */
ssize_t qemu_write_full(int fd, const void *buf, size_t count)
{
    ssize_t ret = 0;
    ssize_t total = 0;

    while (count) {
        ret = write(fd, buf, count);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        count -= ret;
        buf += ret;
        total += ret;
    }

    return total;
}

/*
 * Opens a socket with FD_CLOEXEC set
 */
int qemu_socket(int domain, int type, int protocol)
{
    int ret;

#ifdef SOCK_CLOEXEC
    ret = socket(domain, type | SOCK_CLOEXEC, protocol);
    if (ret != -1 || errno != EINVAL) {
        return ret;
    }
#endif
    ret = socket(domain, type, protocol);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }

    return ret;
}

/*
 * Accept a connection and set FD_CLOEXEC
 */
int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    int ret;

#ifdef CONFIG_ACCEPT4
    ret = accept4(s, addr, addrlen, SOCK_CLOEXEC);
    if (ret != -1 || errno != ENOSYS) {
        return ret;
    }
#endif
    ret = accept(s, addr, addrlen);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }

    return ret;
}

void qemu_set_hw_version(const char *version)
{
    hw_version = version;
}

const char *qemu_hw_version(void)
{
    return hw_version;
}

void fips_set_state(bool requested)
{
#ifdef __linux__
    if (requested) {
        FILE *fds = fopen("/proc/sys/crypto/fips_enabled", "r");
        if (fds != NULL) {
            fips_enabled = (fgetc(fds) == '1');
            fclose(fds);
        }
    }
#else
    fips_enabled = false;
#endif /* __linux__ */

#ifdef _FIPS_DEBUG
    fprintf(stderr, "FIPS mode %s (requested %s)\n",
	    (fips_enabled ? "enabled" : "disabled"),
	    (requested ? "enabled" : "disabled"));
#endif
}

bool fips_get_state(void)
{
    return fips_enabled;
}

#ifdef _WIN32
static void socket_cleanup(void)
{
    WSACleanup();
}
#endif

int socket_init(void)
{
#ifdef _WIN32
    WSADATA Data;
    int ret, err;

    ret = WSAStartup(MAKEWORD(2, 2), &Data);
    if (ret != 0) {
        err = WSAGetLastError();
        fprintf(stderr, "WSAStartup: %d\n", err);
        return -1;
    }
    atexit(socket_cleanup);
#endif
    return 0;
}

#if !GLIB_CHECK_VERSION(2, 31, 0)
/* Ensure that glib is running in multi-threaded mode
 * Old versions of glib require explicit initialization.  Failure to do
 * this results in the single-threaded code paths being taken inside
 * glib.  For example, the g_slice allocator will not be thread-safe
 * and cause crashes.
 */
static void __attribute__((constructor)) thread_init(void)
{
    if (!g_thread_supported()) {
       g_thread_init(NULL);
    }
}
#endif

#ifndef CONFIG_IOVEC
/* helper function for iov_send_recv() */
static ssize_t
readv_writev(int fd, const struct iovec *iov, int iov_cnt, bool do_write)
{
    unsigned i = 0;
    ssize_t ret = 0;
    while (i < iov_cnt) {
        ssize_t r = do_write
            ? write(fd, iov[i].iov_base, iov[i].iov_len)
            : read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r > 0) {
            ret += r;
        } else if (!r) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            /* else it is some "other" error,
             * only return if there was no data processed. */
            if (ret == 0) {
                ret = -1;
            }
            break;
        }
        i++;
    }
    return ret;
}

ssize_t
readv(int fd, const struct iovec *iov, int iov_cnt)
{
    return readv_writev(fd, iov, iov_cnt, false);
}

ssize_t
writev(int fd, const struct iovec *iov, int iov_cnt)
{
    return readv_writev(fd, iov, iov_cnt, true);
}
#endif
