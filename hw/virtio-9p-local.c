/*
 * Virtio 9p Posix callback
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#include "virtio.h"
#include "virtio-9p.h"
#include <pwd.h>
#include <grp.h>

static const char *rpath(FsContext *ctx, const char *path)
{
    /* FIXME: so wrong... */
    static char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s/%s", ctx->fs_root, path);
    return buffer;
}

static int local_lstat(FsContext *ctx, const char *path, struct stat *stbuf)
{
    return lstat(rpath(ctx, path), stbuf);
}

static int local_setuid(FsContext *ctx, uid_t uid)
{
    struct passwd *pw;
    gid_t groups[33];
    int ngroups;
    static uid_t cur_uid = -1;

    if (cur_uid == uid) {
        return 0;
    }

    if (setreuid(0, 0)) {
        return -1;
    }

    pw = getpwuid(uid);
    if (pw == NULL) {
        return -1;
    }

    ngroups = 33;
    if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) == -1) {
        return -1;
    }

    if (setgroups(ngroups, groups)) {
        return -1;
    }

    if (setregid(-1, pw->pw_gid)) {
        return -1;
    }

    if (setreuid(-1, uid)) {
        return -1;
    }

    cur_uid = uid;

    return 0;
}

static ssize_t local_readlink(FsContext *ctx, const char *path,
                                char *buf, size_t bufsz)
{
    return readlink(rpath(ctx, path), buf, bufsz);
}

static int local_close(FsContext *ctx, int fd)
{
    return close(fd);
}

static int local_closedir(FsContext *ctx, DIR *dir)
{
    return closedir(dir);
}

static int local_open(FsContext *ctx, const char *path, int flags)
{
    return open(rpath(ctx, path), flags);
}

static DIR *local_opendir(FsContext *ctx, const char *path)
{
    return opendir(rpath(ctx, path));
}

static void local_rewinddir(FsContext *ctx, DIR *dir)
{
    return rewinddir(dir);
}

static off_t local_telldir(FsContext *ctx, DIR *dir)
{
    return telldir(dir);
}

static struct dirent *local_readdir(FsContext *ctx, DIR *dir)
{
    return readdir(dir);
}

static void local_seekdir(FsContext *ctx, DIR *dir, off_t off)
{
    return seekdir(dir, off);
}

static ssize_t local_readv(FsContext *ctx, int fd, const struct iovec *iov,
                            int iovcnt)
{
    return readv(fd, iov, iovcnt);
}

static off_t local_lseek(FsContext *ctx, int fd, off_t offset, int whence)
{
    return lseek(fd, offset, whence);
}

static ssize_t local_writev(FsContext *ctx, int fd, const struct iovec *iov,
                            int iovcnt)
{
    return writev(fd, iov, iovcnt);
}

FileOperations local_ops = {
    .lstat = local_lstat,
    .setuid = local_setuid,
    .readlink = local_readlink,
    .close = local_close,
    .closedir = local_closedir,
    .open = local_open,
    .opendir = local_opendir,
    .rewinddir = local_rewinddir,
    .telldir = local_telldir,
    .readdir = local_readdir,
    .seekdir = local_seekdir,
    .readv = local_readv,
    .lseek = local_lseek,
    .writev = local_writev,
};
