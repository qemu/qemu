/*
 * 9p utilities (Darwin Implementation)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/xattr.h"
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
