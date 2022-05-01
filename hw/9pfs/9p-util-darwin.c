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

static int create_socket_file_at_cwd(const char *filename, mode_t mode) {
    int fd, err;
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX
    };

    err = snprintf(addr.sun_path, sizeof(addr.sun_path), "./%s", filename);
    if (err < 0 || err >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (fd == -1) {
        return fd;
    }
    err = bind(fd, (struct sockaddr *) &addr, sizeof(addr));
    if (err == -1) {
        goto out;
    }
    /*
     * FIXME: Should rather be using descriptor-based fchmod() on the
     * socket file descriptor above (preferably before bind() call),
     * instead of path-based fchmodat(), to prevent concurrent transient
     * state issues between creating the named FIFO file at bind() and
     * delayed adjustment of permissions at fchmodat(). However currently
     * macOS (12.x) does not support such operations on socket file
     * descriptors yet.
     *
     * Filed report with Apple: FB9997731
     */
    err = fchmodat(AT_FDCWD, filename, mode, AT_SYMLINK_NOFOLLOW);
out:
    close_preserve_errno(fd);
    return err;
}

int qemu_mknodat(int dirfd, const char *filename, mode_t mode, dev_t dev)
{
    int preserved_errno, err;

    if (S_ISREG(mode) || !(mode & S_IFMT)) {
        int fd = openat_file(dirfd, filename, O_CREAT, mode);
        if (fd == -1) {
            return fd;
        }
        close(fd);
        return 0;
    }
    if (!pthread_fchdir_np) {
        error_report_once("pthread_fchdir_np() not available on this version of macOS");
        errno = ENOTSUP;
        return -1;
    }
    if (pthread_fchdir_np(dirfd) < 0) {
        return -1;
    }
    if (S_ISSOCK(mode)) {
        err = create_socket_file_at_cwd(filename, mode);
    } else {
        err = mknod(filename, mode, dev);
    }
    preserved_errno = errno;
    /* Stop using the thread-local cwd */
    pthread_fchdir_np(-1);
    if (err < 0) {
        errno = preserved_errno;
    }
    return err;
}

#endif
