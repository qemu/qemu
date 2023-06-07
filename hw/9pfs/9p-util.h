/*
 * 9p utilities
 *
 * Copyright IBM, Corp. 2017
 *
 * Authors:
 *  Greg Kurz <groug@kaod.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_9P_UTIL_H
#define QEMU_9P_UTIL_H

#include "qemu/error-report.h"

#ifdef O_PATH
#define O_PATH_9P_UTIL O_PATH
#else
#define O_PATH_9P_UTIL 0
#endif

#if !defined(CONFIG_LINUX)

/*
 * Generates a Linux device number (a.k.a. dev_t) for given device major
 * and minor numbers.
 *
 * To be more precise: it generates a device number in glibc's format
 * (MMMM_Mmmm_mmmM_MMmm, 64 bits) actually, which is compatible with
 * Linux's format (mmmM_MMmm, 32 bits), as described in <bits/sysmacros.h>.
 */
static inline uint64_t makedev_dotl(uint32_t dev_major, uint32_t dev_minor)
{
    uint64_t dev;

    // from glibc sysmacros.h:
    dev  = (((uint64_t) (dev_major & 0x00000fffu)) <<  8);
    dev |= (((uint64_t) (dev_major & 0xfffff000u)) << 32);
    dev |= (((uint64_t) (dev_minor & 0x000000ffu)) <<  0);
    dev |= (((uint64_t) (dev_minor & 0xffffff00u)) << 12);
    return dev;
}

#endif

/*
 * Converts given device number from host's device number format to Linux
 * device number format. As both the size of type dev_t and encoding of
 * dev_t is system dependant, we have to convert them for Linux guests if
 * host is not running Linux.
 */
static inline uint64_t host_dev_to_dotl_dev(dev_t dev)
{
#ifdef CONFIG_LINUX
    return dev;
#else
    return makedev_dotl(major(dev), minor(dev));
#endif
}

/* Translates errno from host -> Linux if needed */
static inline int errno_to_dotl(int err) {
#if defined(CONFIG_LINUX)
    /* nothing to translate (Linux -> Linux) */
#elif defined(CONFIG_DARWIN)
    /*
     * translation mandatory for macOS hosts
     *
     * FIXME: Only most important errnos translated here yet, this should be
     * extended to as many errnos being translated as possible in future.
     */
    if (err == ENAMETOOLONG) {
        err = 36; /* ==ENAMETOOLONG on Linux */
    } else if (err == ENOTEMPTY) {
        err = 39; /* ==ENOTEMPTY on Linux */
    } else if (err == ELOOP) {
        err = 40; /* ==ELOOP on Linux */
    } else if (err == ENOATTR) {
        err = 61; /* ==ENODATA on Linux */
    } else if (err == ENOTSUP) {
        err = 95; /* ==EOPNOTSUPP on Linux */
    } else if (err == EOPNOTSUPP) {
        err = 95; /* ==EOPNOTSUPP on Linux */
    }
#else
#error Missing errno translation to Linux for this host system
#endif
    return err;
}

#ifdef CONFIG_DARWIN
#define qemu_fgetxattr(...) fgetxattr(__VA_ARGS__, 0, 0)
#define qemu_lgetxattr(...) getxattr(__VA_ARGS__, 0, XATTR_NOFOLLOW)
#define qemu_llistxattr(...) listxattr(__VA_ARGS__, XATTR_NOFOLLOW)
#define qemu_lremovexattr(...) removexattr(__VA_ARGS__, XATTR_NOFOLLOW)
static inline int qemu_lsetxattr(const char *path, const char *name,
                                 const void *value, size_t size, int flags) {
    return setxattr(path, name, value, size, 0, flags | XATTR_NOFOLLOW);
}
#else
#define qemu_fgetxattr fgetxattr
#define qemu_lgetxattr lgetxattr
#define qemu_llistxattr llistxattr
#define qemu_lremovexattr lremovexattr
#define qemu_lsetxattr lsetxattr
#endif

static inline void close_preserve_errno(int fd)
{
    int serrno = errno;
    close(fd);
    errno = serrno;
}

/**
 * close_if_special_file() - Close @fd if neither regular file nor directory.
 *
 * @fd: file descriptor of open file
 * Return: 0 on regular file or directory, -1 otherwise
 *
 * CVE-2023-2861: Prohibit opening any special file directly on host
 * (especially device files), as a compromised client could potentially gain
 * access outside exported tree under certain, unsafe setups. We expect
 * client to handle I/O on special files exclusively on guest side.
 */
static inline int close_if_special_file(int fd)
{
    struct stat stbuf;

    if (fstat(fd, &stbuf) < 0) {
        close_preserve_errno(fd);
        return -1;
    }
    if (!S_ISREG(stbuf.st_mode) && !S_ISDIR(stbuf.st_mode)) {
        error_report_once(
            "9p: broken or compromised client detected; attempt to open "
            "special file (i.e. neither regular file, nor directory)"
        );
        close(fd);
        errno = ENXIO;
        return -1;
    }

    return 0;
}

static inline int openat_dir(int dirfd, const char *name)
{
    return openat(dirfd, name,
                  O_DIRECTORY | O_RDONLY | O_NOFOLLOW | O_PATH_9P_UTIL);
}

static inline int openat_file(int dirfd, const char *name, int flags,
                              mode_t mode)
{
    int fd, serrno, ret;

#ifndef CONFIG_DARWIN
again:
#endif
    fd = openat(dirfd, name, flags | O_NOFOLLOW | O_NOCTTY | O_NONBLOCK,
                mode);
    if (fd == -1) {
#ifndef CONFIG_DARWIN
        if (errno == EPERM && (flags & O_NOATIME)) {
            /*
             * The client passed O_NOATIME but we lack permissions to honor it.
             * Rather than failing the open, fall back without O_NOATIME. This
             * doesn't break the semantics on the client side, as the Linux
             * open(2) man page notes that O_NOATIME "may not be effective on
             * all filesystems". In particular, NFS and other network
             * filesystems ignore it entirely.
             */
            flags &= ~O_NOATIME;
            goto again;
        }
#endif
        return -1;
    }

    if (close_if_special_file(fd) < 0) {
        return -1;
    }

    serrno = errno;
    /* O_NONBLOCK was only needed to open the file. Let's drop it. We don't
     * do that with O_PATH since fcntl(F_SETFL) isn't supported, and openat()
     * ignored it anyway.
     */
    if (!(flags & O_PATH_9P_UTIL)) {
        ret = fcntl(fd, F_SETFL, flags);
        assert(!ret);
    }
    errno = serrno;
    return fd;
}

ssize_t fgetxattrat_nofollow(int dirfd, const char *path, const char *name,
                             void *value, size_t size);
int fsetxattrat_nofollow(int dirfd, const char *path, const char *name,
                         void *value, size_t size, int flags);
ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size);
ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name);

/*
 * Darwin has d_seekoff, which appears to function similarly to d_off.
 * However, it does not appear to be supported on all file systems,
 * so ensure it is manually injected earlier and call here when
 * needed.
 */
static inline off_t qemu_dirent_off(struct dirent *dent)
{
#ifdef CONFIG_DARWIN
    return dent->d_seekoff;
#else
    return dent->d_off;
#endif
}

/**
 * qemu_dirent_dup() - Duplicate directory entry @dent.
 *
 * @dent: original directory entry to be duplicated
 * Return: duplicated directory entry which should be freed with g_free()
 *
 * It is highly recommended to use this function instead of open coding
 * duplication of dirent objects, because the actual struct dirent
 * size may be bigger or shorter than sizeof(struct dirent) and correct
 * handling is platform specific (see gitlab issue #841).
 */
static inline struct dirent *qemu_dirent_dup(struct dirent *dent)
{
    size_t sz = 0;
#if defined _DIRENT_HAVE_D_RECLEN
    /* Avoid use of strlen() if platform supports d_reclen. */
    sz = dent->d_reclen;
#endif
    /*
     * Test sz for zero even if d_reclen is available
     * because some drivers may set d_reclen to zero.
     */
    if (sz == 0) {
        /* Fallback to the most portable way. */
        sz = offsetof(struct dirent, d_name) +
                      strlen(dent->d_name) + 1;
    }
    return g_memdup(dent, sz);
}

/*
 * As long as mknodat is not available on macOS, this workaround
 * using pthread_fchdir_np is needed. qemu_mknodat is defined in
 * os-posix.c. pthread_fchdir_np is weakly linked here as a guard
 * in case it disappears in future macOS versions, because it is
 * is a private API.
 */
#if defined CONFIG_DARWIN && defined CONFIG_PTHREAD_FCHDIR_NP
int pthread_fchdir_np(int fd) __attribute__((weak_import));
#endif
int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev);

#endif
