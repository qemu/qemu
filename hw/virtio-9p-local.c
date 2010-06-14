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
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <attr/xattr.h>

static const char *rpath(FsContext *ctx, const char *path)
{
    /* FIXME: so wrong... */
    static char buffer[4096];
    snprintf(buffer, sizeof(buffer), "%s/%s", ctx->fs_root, path);
    return buffer;
}


static int local_lstat(FsContext *fs_ctx, const char *path, struct stat *stbuf)
{
    int err;
    err =  lstat(rpath(fs_ctx, path), stbuf);
    if (err) {
        return err;
    }
    if (fs_ctx->fs_sm == SM_MAPPED) {
        /* Actual credentials are part of extended attrs */
        uid_t tmp_uid;
        gid_t tmp_gid;
        mode_t tmp_mode;
        dev_t tmp_dev;
        if (getxattr(rpath(fs_ctx, path), "user.virtfs.uid", &tmp_uid,
                    sizeof(uid_t)) > 0) {
            stbuf->st_uid = tmp_uid;
        }
        if (getxattr(rpath(fs_ctx, path), "user.virtfs.gid", &tmp_gid,
                    sizeof(gid_t)) > 0) {
            stbuf->st_gid = tmp_gid;
        }
        if (getxattr(rpath(fs_ctx, path), "user.virtfs.mode", &tmp_mode,
                    sizeof(mode_t)) > 0) {
            stbuf->st_mode = tmp_mode;
        }
        if (getxattr(rpath(fs_ctx, path), "user.virtfs.rdev", &tmp_dev,
                        sizeof(dev_t)) > 0) {
                stbuf->st_rdev = tmp_dev;
        }
    }
    return err;
}

static int local_set_xattr(const char *path, FsCred *credp)
{
    int err;
    if (credp->fc_uid != -1) {
        err = setxattr(path, "user.virtfs.uid", &credp->fc_uid, sizeof(uid_t),
                0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_gid != -1) {
        err = setxattr(path, "user.virtfs.gid", &credp->fc_gid, sizeof(gid_t),
                0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_mode != -1) {
        err = setxattr(path, "user.virtfs.mode", &credp->fc_mode,
                sizeof(mode_t), 0);
        if (err) {
            return err;
        }
    }
    if (credp->fc_rdev != -1) {
        err = setxattr(path, "user.virtfs.rdev", &credp->fc_rdev,
                sizeof(dev_t), 0);
        if (err) {
            return err;
        }
    }
    return 0;
}

static int local_post_create_passthrough(FsContext *fs_ctx, const char *path,
        FsCred *credp)
{
    if (chmod(rpath(fs_ctx, path), credp->fc_mode & 07777) < 0) {
        return -1;
    }
    if (chown(rpath(fs_ctx, path), credp->fc_uid, credp->fc_gid) < 0) {
        return -1;
    }
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

static int local_chmod(FsContext *fs_ctx, const char *path, FsCred *credp)
{
    if (fs_ctx->fs_sm == SM_MAPPED) {
        return local_set_xattr(rpath(fs_ctx, path), credp);
    } else if (fs_ctx->fs_sm == SM_PASSTHROUGH) {
        return chmod(rpath(fs_ctx, path), credp->fc_mode);
    }
    return -1;
}

static int local_mknod(FsContext *ctx, const char *path, mode_t mode, dev_t dev)
{
    return mknod(rpath(ctx, path), mode, dev);
}

static int local_mksock(FsContext *ctx2, const char *path)
{
    struct sockaddr_un addr;
    int s;

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, 108, "%s", rpath(ctx2, path));

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        return -1;
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
        close(s);
        return -1;
    }

    close(s);
    return 0;
}

static int local_mkdir(FsContext *ctx, const char *path, mode_t mode)
{
    return mkdir(rpath(ctx, path), mode);
}

static int local_fstat(FsContext *fs_ctx, int fd, struct stat *stbuf)
{
    int err;
    err = fstat(fd, stbuf);
    if (err) {
        return err;
    }
    if (fs_ctx->fs_sm == SM_MAPPED) {
        /* Actual credentials are part of extended attrs */
        uid_t tmp_uid;
        gid_t tmp_gid;
        mode_t tmp_mode;
        dev_t tmp_dev;

        if (fgetxattr(fd, "user.virtfs.uid", &tmp_uid, sizeof(uid_t)) > 0) {
            stbuf->st_uid = tmp_uid;
        }
        if (fgetxattr(fd, "user.virtfs.gid", &tmp_gid, sizeof(gid_t)) > 0) {
            stbuf->st_gid = tmp_gid;
        }
        if (fgetxattr(fd, "user.virtfs.mode", &tmp_mode, sizeof(mode_t)) > 0) {
            stbuf->st_mode = tmp_mode;
        }
        if (fgetxattr(fd, "user.virtfs.rdev", &tmp_dev, sizeof(dev_t)) > 0) {
                stbuf->st_rdev = tmp_dev;
        }
    }
    return err;
}

static int local_open2(FsContext *fs_ctx, const char *path, int flags,
        FsCred *credp)
{
    int fd = -1;
    int err = -1;
    int serrno = 0;

    /* Determine the security model */
    if (fs_ctx->fs_sm == SM_MAPPED) {
        fd = open(rpath(fs_ctx, path), flags, SM_LOCAL_MODE_BITS);
        if (fd == -1) {
            return fd;
        }
        credp->fc_mode = credp->fc_mode|S_IFREG;
        /* Set cleint credentials in xattr */
        err = local_set_xattr(rpath(fs_ctx, path), credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    } else if (fs_ctx->fs_sm == SM_PASSTHROUGH) {
        fd = open(rpath(fs_ctx, path), flags, credp->fc_mode);
        if (fd == -1) {
            return fd;
        }
        err = local_post_create_passthrough(fs_ctx, path, credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    }
    return fd;

err_end:
    close(fd);
    remove(rpath(fs_ctx, path));
    errno = serrno;
    return err;
}


static int local_symlink(FsContext *ctx, const char *oldpath,
                            const char *newpath)
{
    return symlink(oldpath, rpath(ctx, newpath));
}

static int local_link(FsContext *ctx, const char *oldpath, const char *newpath)
{
    char *tmp = qemu_strdup(rpath(ctx, oldpath));
    int err, serrno = 0;

    if (tmp == NULL) {
        return -ENOMEM;
    }

    err = link(tmp, rpath(ctx, newpath));
    if (err == -1) {
        serrno = errno;
    }

    qemu_free(tmp);

    if (err == -1) {
        errno = serrno;
    }

    return err;
}

static int local_truncate(FsContext *ctx, const char *path, off_t size)
{
    return truncate(rpath(ctx, path), size);
}

static int local_rename(FsContext *ctx, const char *oldpath,
                        const char *newpath)
{
    char *tmp;
    int err;

    tmp = qemu_strdup(rpath(ctx, oldpath));
    if (tmp == NULL) {
        return -1;
    }

    err = rename(tmp, rpath(ctx, newpath));
    if (err == -1) {
        int serrno = errno;
        qemu_free(tmp);
        errno = serrno;
    } else {
        qemu_free(tmp);
    }

    return err;

}

static int local_chown(FsContext *fs_ctx, const char *path, FsCred *credp)
{
    if (fs_ctx->fs_sm == SM_MAPPED) {
        return local_set_xattr(rpath(fs_ctx, path), credp);
    } else if (fs_ctx->fs_sm == SM_PASSTHROUGH) {
        return lchown(rpath(fs_ctx, path), credp->fc_uid, credp->fc_gid);
    }
    return -1;
}

static int local_utime(FsContext *ctx, const char *path,
                        const struct utimbuf *buf)
{
    return utime(rpath(ctx, path), buf);
}

static int local_remove(FsContext *ctx, const char *path)
{
    return remove(rpath(ctx, path));
}

static int local_fsync(FsContext *ctx, int fd)
{
    return fsync(fd);
}

FileOperations local_ops = {
    .lstat = local_lstat,
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
    .chmod = local_chmod,
    .mknod = local_mknod,
    .mksock = local_mksock,
    .mkdir = local_mkdir,
    .fstat = local_fstat,
    .open2 = local_open2,
    .symlink = local_symlink,
    .link = local_link,
    .truncate = local_truncate,
    .rename = local_rename,
    .chown = local_chown,
    .utime = local_utime,
    .remove = local_remove,
    .fsync = local_fsync,
};
