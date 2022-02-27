/*
 * 9p utilities (Darwin Implementation)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/xattr.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "9p-util.h"

ssize_t fgetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                             void *value, size_t size)
{
    int ret;
    int fd = openat_file(dirfd, filename,
                         O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    ret = fgetxattr(fd, name, value, size, 0, 0);
    close_preserve_errno(fd);
    return ret;
}

ssize_t flistxattrat_nofollow(int dirfd, const char *filename,
                              char *list, size_t size)
{
    int ret;
    int fd = openat_file(dirfd, filename,
                         O_RDONLY | O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    ret = flistxattr(fd, list, size, 0);
    close_preserve_errno(fd);
    return ret;
}

ssize_t fremovexattrat_nofollow(int dirfd, const char *filename,
                                const char *name)
{
    int ret;
    int fd = openat_file(dirfd, filename, O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    ret = fremovexattr(fd, name, 0);
    close_preserve_errno(fd);
    return ret;
}

int fsetxattrat_nofollow(int dirfd, const char *filename, const char *name,
                         void *value, size_t size, int flags)
{
    int ret;
    int fd = openat_file(dirfd, filename, O_PATH_9P_UTIL | O_NOFOLLOW, 0);
    if (fd == -1) {
        return -1;
    }
    ret = fsetxattr(fd, name, value, size, 0, flags);
    close_preserve_errno(fd);
    return ret;
}

/*
 * As long as mknodat is not available on macOS, this workaround
 * using pthread_fchdir_np is needed.
 *
 * Radar filed with Apple for implementing mknodat:
 * rdar://FB9862426 (https://openradar.appspot.com/FB9862426)
 */
#if defined CONFIG_PTHREAD_FCHDIR_NP

int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev)
{
    int preserved_errno, err;
    if (!pthread_fchdir_np) {
        error_report_once("pthread_fchdir_np() not available on this version of macOS");
        return -ENOTSUP;
    }
    if (pthread_fchdir_np(dirfd) < 0) {
        return -1;
    }
    err = mknod(filename, mode, dev);
    preserved_errno = errno;
    /* Stop using the thread-local cwd */
    pthread_fchdir_np(-1);
    if (err < 0) {
        errno = preserved_errno;
    }
    return err;
}

#endif
