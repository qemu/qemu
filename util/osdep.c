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
#include "qapi/error.h"

/* Needed early for CONFIG_BSD etc. */

#ifdef CONFIG_SOLARIS
#include <sys/statvfs.h>
/* See MySQL bug #7156 (http://bugs.mysql.com/bug.php?id=7156) for
   discussion about Solaris header problems */
extern int madvise(char *, size_t, int);
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

static int qemu_mprotect__osdep(void *addr, size_t size, int prot)
{
    g_assert(!((uintptr_t)addr & ~qemu_real_host_page_mask));
    g_assert(!(size & ~qemu_real_host_page_mask));

#ifdef _WIN32
    DWORD old_protect;

    if (!VirtualProtect(addr, size, prot, &old_protect)) {
        g_autofree gchar *emsg = g_win32_error_message(GetLastError());
        error_report("%s: VirtualProtect failed: %s", __func__, emsg);
        return -1;
    }
    return 0;
#else
    if (mprotect(addr, size, prot)) {
        error_report("%s: mprotect failed: %s", __func__, strerror(errno));
        return -1;
    }
    return 0;
#endif
}

int qemu_mprotect_rwx(void *addr, size_t size)
{
#ifdef _WIN32
    return qemu_mprotect__osdep(addr, size, PAGE_EXECUTE_READWRITE);
#else
    return qemu_mprotect__osdep(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
}

int qemu_mprotect_none(void *addr, size_t size)
{
#ifdef _WIN32
    return qemu_mprotect__osdep(addr, size, PAGE_NOACCESS);
#else
    return qemu_mprotect__osdep(addr, size, PROT_NONE);
#endif
}

#ifndef _WIN32

static int fcntl_op_setlk = -1;
static int fcntl_op_getlk = -1;

/*
 * Dups an fd and sets the flags
 */
int qemu_dup_flags(int fd, int flags)
{
    int ret;
    int serrno;
    int dup_flags;

    ret = qemu_dup(fd);
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

int qemu_dup(int fd)
{
    int ret;
#ifdef F_DUPFD_CLOEXEC
    ret = fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
    ret = dup(fd);
    if (ret != -1) {
        qemu_set_cloexec(ret);
    }
#endif
    return ret;
}

static int qemu_parse_fdset(const char *param)
{
    return qemu_parse_fd(param);
}

static void qemu_probe_lock_ops(void)
{
    if (fcntl_op_setlk == -1) {
#ifdef F_OFD_SETLK
        int fd;
        int ret;
        struct flock fl = {
            .l_whence = SEEK_SET,
            .l_start  = 0,
            .l_len    = 0,
            .l_type   = F_WRLCK,
        };

        fd = open("/dev/null", O_RDWR);
        if (fd < 0) {
            fprintf(stderr,
                    "Failed to open /dev/null for OFD lock probing: %s\n",
                    strerror(errno));
            fcntl_op_setlk = F_SETLK;
            fcntl_op_getlk = F_GETLK;
            return;
        }
        ret = fcntl(fd, F_OFD_GETLK, &fl);
        close(fd);
        if (!ret) {
            fcntl_op_setlk = F_OFD_SETLK;
            fcntl_op_getlk = F_OFD_GETLK;
        } else {
            fcntl_op_setlk = F_SETLK;
            fcntl_op_getlk = F_GETLK;
        }
#else
        fcntl_op_setlk = F_SETLK;
        fcntl_op_getlk = F_GETLK;
#endif
    }
}

bool qemu_has_ofd_lock(void)
{
    qemu_probe_lock_ops();
#ifdef F_OFD_SETLK
    return fcntl_op_setlk == F_OFD_SETLK;
#else
    return false;
#endif
}

static int qemu_lock_fcntl(int fd, int64_t start, int64_t len, int fl_type)
{
    int ret;
    struct flock fl = {
        .l_whence = SEEK_SET,
        .l_start  = start,
        .l_len    = len,
        .l_type   = fl_type,
    };
    qemu_probe_lock_ops();
    do {
        ret = fcntl(fd, fcntl_op_setlk, &fl);
    } while (ret == -1 && errno == EINTR);
    return ret == -1 ? -errno : 0;
}

int qemu_lock_fd(int fd, int64_t start, int64_t len, bool exclusive)
{
    return qemu_lock_fcntl(fd, start, len, exclusive ? F_WRLCK : F_RDLCK);
}

int qemu_unlock_fd(int fd, int64_t start, int64_t len)
{
    return qemu_lock_fcntl(fd, start, len, F_UNLCK);
}

int qemu_lock_fd_test(int fd, int64_t start, int64_t len, bool exclusive)
{
    int ret;
    struct flock fl = {
        .l_whence = SEEK_SET,
        .l_start  = start,
        .l_len    = len,
        .l_type   = exclusive ? F_WRLCK : F_RDLCK,
    };
    qemu_probe_lock_ops();
    ret = fcntl(fd, fcntl_op_getlk, &fl);
    if (ret == -1) {
        return -errno;
    } else {
        return fl.l_type == F_UNLCK ? 0 : -EAGAIN;
    }
}
#endif

static int qemu_open_cloexec(const char *name, int flags, mode_t mode)
{
    int ret;
#ifdef O_CLOEXEC
    ret = open(name, flags | O_CLOEXEC, mode);
#else
    ret = open(name, flags, mode);
    if (ret >= 0) {
        qemu_set_cloexec(ret);
    }
#endif
    return ret;
}

/*
 * Opens a file with FD_CLOEXEC set
 */
static int
qemu_open_internal(const char *name, int flags, mode_t mode, Error **errp)
{
    int ret;

#ifndef _WIN32
    const char *fdset_id_str;

    /* Attempt dup of fd from fd set */
    if (strstart(name, "/dev/fdset/", &fdset_id_str)) {
        int64_t fdset_id;
        int dupfd;

        fdset_id = qemu_parse_fdset(fdset_id_str);
        if (fdset_id == -1) {
            error_setg(errp, "Could not parse fdset %s", name);
            errno = EINVAL;
            return -1;
        }

        dupfd = monitor_fdset_dup_fd_add(fdset_id, flags);
        if (dupfd == -1) {
            error_setg_errno(errp, errno, "Could not dup FD for %s flags %x",
                             name, flags);
            return -1;
        }

        return dupfd;
    }
#endif

    ret = qemu_open_cloexec(name, flags, mode);

    if (ret == -1) {
        const char *action = flags & O_CREAT ? "create" : "open";
#ifdef O_DIRECT
        /* Give more helpful error message for O_DIRECT */
        if (errno == EINVAL && (flags & O_DIRECT)) {
            ret = open(name, flags & ~O_DIRECT, mode);
            if (ret != -1) {
                close(ret);
                error_setg(errp, "Could not %s '%s': "
                           "filesystem does not support O_DIRECT",
                           action, name);
                errno = EINVAL; /* restore first open()'s errno */
                return -1;
            }
        }
#endif /* O_DIRECT */
        error_setg_errno(errp, errno, "Could not %s '%s'",
                         action, name);
    }

    return ret;
}


int qemu_open(const char *name, int flags, Error **errp)
{
    assert(!(flags & O_CREAT));

    return qemu_open_internal(name, flags, 0, errp);
}


int qemu_create(const char *name, int flags, mode_t mode, Error **errp)
{
    assert(!(flags & O_CREAT));

    return qemu_open_internal(name, flags | O_CREAT, mode, errp);
}


int qemu_open_old(const char *name, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;
    int ret;

    va_start(ap, flags);
    if (flags & O_CREAT) {
        mode = va_arg(ap, int);
    }
    va_end(ap);

    ret = qemu_open_internal(name, flags, mode, NULL);

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
 * Delete a file from the filesystem, unless the filename is /dev/fdset/...
 *
 * Returns: On success, zero is returned.  On error, -1 is returned,
 * and errno is set appropriately.
 */
int qemu_unlink(const char *name)
{
    if (g_str_has_prefix(name, "/dev/fdset/")) {
        return 0;
    }

    return unlink(name);
}

/*
 * A variant of write(2) which handles partial write.
 *
 * Return the number of bytes transferred.
 * Set errno if fewer than `count' bytes are written.
 *
 * This function don't work with non-blocking fd's.
 * Any of the possibilities with non-blocking fd's is bad:
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
