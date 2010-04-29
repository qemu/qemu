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

FileOperations local_ops = {
    .lstat = local_lstat,
    .setuid = local_setuid,
    .readlink = local_readlink,
    .close = local_close,
    .closedir = local_closedir,
};
