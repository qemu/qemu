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

#include "hw/virtio.h"
#include "virtio-9p.h"
#include "virtio-9p-xattr.h"
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <attr/xattr.h>


static int local_lstat(FsContext *fs_ctx, V9fsPath *fs_path, struct stat *stbuf)
{
    int err;
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    err =  lstat(rpath(fs_ctx, path, buffer), stbuf);
    if (err) {
        return err;
    }
    if (fs_ctx->fs_sm == SM_MAPPED) {
        /* Actual credentials are part of extended attrs */
        uid_t tmp_uid;
        gid_t tmp_gid;
        mode_t tmp_mode;
        dev_t tmp_dev;
        if (getxattr(rpath(fs_ctx, path, buffer), "user.virtfs.uid", &tmp_uid,
                    sizeof(uid_t)) > 0) {
            stbuf->st_uid = tmp_uid;
        }
        if (getxattr(rpath(fs_ctx, path, buffer), "user.virtfs.gid", &tmp_gid,
                    sizeof(gid_t)) > 0) {
            stbuf->st_gid = tmp_gid;
        }
        if (getxattr(rpath(fs_ctx, path, buffer), "user.virtfs.mode",
                    &tmp_mode, sizeof(mode_t)) > 0) {
            stbuf->st_mode = tmp_mode;
        }
        if (getxattr(rpath(fs_ctx, path, buffer), "user.virtfs.rdev", &tmp_dev,
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
    char buffer[PATH_MAX];

    if (chmod(rpath(fs_ctx, path, buffer), credp->fc_mode & 07777) < 0) {
        return -1;
    }
    if (lchown(rpath(fs_ctx, path, buffer), credp->fc_uid,
                credp->fc_gid) < 0) {
        /*
         * If we fail to change ownership and if we are
         * using security model none. Ignore the error
         */
        if (fs_ctx->fs_sm != SM_NONE) {
            return -1;
        }
    }
    return 0;
}

static ssize_t local_readlink(FsContext *fs_ctx, V9fsPath *fs_path,
                              char *buf, size_t bufsz)
{
    ssize_t tsize = -1;
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    if (fs_ctx->fs_sm == SM_MAPPED) {
        int fd;
        fd = open(rpath(fs_ctx, path, buffer), O_RDONLY);
        if (fd == -1) {
            return -1;
        }
        do {
            tsize = read(fd, (void *)buf, bufsz);
        } while (tsize == -1 && errno == EINTR);
        close(fd);
        return tsize;
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        tsize = readlink(rpath(fs_ctx, path, buffer), buf, bufsz);
    }
    return tsize;
}

static int local_close(FsContext *ctx, int fd)
{
    return close(fd);
}

static int local_closedir(FsContext *ctx, DIR *dir)
{
    return closedir(dir);
}

static int local_open(FsContext *ctx, V9fsPath *fs_path, int flags)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    return open(rpath(ctx, path, buffer), flags);
}

static DIR *local_opendir(FsContext *ctx, V9fsPath *fs_path)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    return opendir(rpath(ctx, path, buffer));
}

static void local_rewinddir(FsContext *ctx, DIR *dir)
{
    return rewinddir(dir);
}

static off_t local_telldir(FsContext *ctx, DIR *dir)
{
    return telldir(dir);
}

static int local_readdir_r(FsContext *ctx, DIR *dir, struct dirent *entry,
                           struct dirent **result)
{
    return readdir_r(dir, entry, result);
}

static void local_seekdir(FsContext *ctx, DIR *dir, off_t off)
{
    return seekdir(dir, off);
}

static ssize_t local_preadv(FsContext *ctx, int fd, const struct iovec *iov,
                            int iovcnt, off_t offset)
{
#ifdef CONFIG_PREADV
    return preadv(fd, iov, iovcnt, offset);
#else
    int err = lseek(fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        return readv(fd, iov, iovcnt);
    }
#endif
}

static ssize_t local_pwritev(FsContext *ctx, int fd, const struct iovec *iov,
                             int iovcnt, off_t offset)
{
#ifdef CONFIG_PREADV
    return pwritev(fd, iov, iovcnt, offset);
#else
    int err = lseek(fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        return writev(fd, iov, iovcnt);
    }
#endif
}

static int local_chmod(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    if (fs_ctx->fs_sm == SM_MAPPED) {
        return local_set_xattr(rpath(fs_ctx, path, buffer), credp);
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        return chmod(rpath(fs_ctx, path, buffer), credp->fc_mode);
    }
    return -1;
}

static int local_mknod(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    char *path;
    int err = -1;
    int serrno = 0;
    V9fsString fullname;
    char buffer[PATH_MAX];

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);
    path = fullname.data;

    /* Determine the security model */
    if (fs_ctx->fs_sm == SM_MAPPED) {
        err = mknod(rpath(fs_ctx, path, buffer),
                SM_LOCAL_MODE_BITS|S_IFREG, 0);
        if (err == -1) {
            goto out;
        }
        local_set_xattr(rpath(fs_ctx, path, buffer), credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        err = mknod(rpath(fs_ctx, path, buffer), credp->fc_mode,
                credp->fc_rdev);
        if (err == -1) {
            goto out;
        }
        err = local_post_create_passthrough(fs_ctx, path, credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    }
    goto out;

err_end:
    remove(rpath(fs_ctx, path, buffer));
    errno = serrno;
out:
    v9fs_string_free(&fullname);
    return err;
}

static int local_mkdir(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    char *path;
    int err = -1;
    int serrno = 0;
    V9fsString fullname;
    char buffer[PATH_MAX];

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);
    path = fullname.data;

    /* Determine the security model */
    if (fs_ctx->fs_sm == SM_MAPPED) {
        err = mkdir(rpath(fs_ctx, path, buffer), SM_LOCAL_DIR_MODE_BITS);
        if (err == -1) {
            goto out;
        }
        credp->fc_mode = credp->fc_mode|S_IFDIR;
        err = local_set_xattr(rpath(fs_ctx, path, buffer), credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        err = mkdir(rpath(fs_ctx, path, buffer), credp->fc_mode);
        if (err == -1) {
            goto out;
        }
        err = local_post_create_passthrough(fs_ctx, path, credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    }
    goto out;

err_end:
    remove(rpath(fs_ctx, path, buffer));
    errno = serrno;
out:
    v9fs_string_free(&fullname);
    return err;
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

static int local_open2(FsContext *fs_ctx, V9fsPath *dir_path, const char *name,
                       int flags, FsCred *credp)
{
    char *path;
    int fd = -1;
    int err = -1;
    int serrno = 0;
    V9fsString fullname;
    char buffer[PATH_MAX];

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);
    path = fullname.data;

    /* Determine the security model */
    if (fs_ctx->fs_sm == SM_MAPPED) {
        fd = open(rpath(fs_ctx, path, buffer), flags, SM_LOCAL_MODE_BITS);
        if (fd == -1) {
            err = fd;
            goto out;
        }
        credp->fc_mode = credp->fc_mode|S_IFREG;
        /* Set cleint credentials in xattr */
        err = local_set_xattr(rpath(fs_ctx, path, buffer), credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        fd = open(rpath(fs_ctx, path, buffer), flags, credp->fc_mode);
        if (fd == -1) {
            err = fd;
            goto out;
        }
        err = local_post_create_passthrough(fs_ctx, path, credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    }
    err = fd;
    goto out;

err_end:
    close(fd);
    remove(rpath(fs_ctx, path, buffer));
    errno = serrno;
out:
    v9fs_string_free(&fullname);
    return err;
}


static int local_symlink(FsContext *fs_ctx, const char *oldpath,
                         V9fsPath *dir_path, const char *name, FsCred *credp)
{
    int err = -1;
    int serrno = 0;
    char *newpath;
    V9fsString fullname;
    char buffer[PATH_MAX];

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);
    newpath = fullname.data;

    /* Determine the security model */
    if (fs_ctx->fs_sm == SM_MAPPED) {
        int fd;
        ssize_t oldpath_size, write_size;
        fd = open(rpath(fs_ctx, newpath, buffer), O_CREAT|O_EXCL|O_RDWR,
                SM_LOCAL_MODE_BITS);
        if (fd == -1) {
            err = fd;
            goto out;
        }
        /* Write the oldpath (target) to the file. */
        oldpath_size = strlen(oldpath);
        do {
            write_size = write(fd, (void *)oldpath, oldpath_size);
        } while (write_size == -1 && errno == EINTR);

        if (write_size != oldpath_size) {
            serrno = errno;
            close(fd);
            err = -1;
            goto err_end;
        }
        close(fd);
        /* Set cleint credentials in symlink's xattr */
        credp->fc_mode = credp->fc_mode|S_IFLNK;
        err = local_set_xattr(rpath(fs_ctx, newpath, buffer), credp);
        if (err == -1) {
            serrno = errno;
            goto err_end;
        }
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        err = symlink(oldpath, rpath(fs_ctx, newpath, buffer));
        if (err) {
            goto out;
        }
        err = lchown(rpath(fs_ctx, newpath, buffer), credp->fc_uid,
                     credp->fc_gid);
        if (err == -1) {
            /*
             * If we fail to change ownership and if we are
             * using security model none. Ignore the error
             */
            if (fs_ctx->fs_sm != SM_NONE) {
                serrno = errno;
                goto err_end;
            } else
                err = 0;
        }
    }
    goto out;

err_end:
    remove(rpath(fs_ctx, newpath, buffer));
    errno = serrno;
out:
    v9fs_string_free(&fullname);
    return err;
}

static int local_link(FsContext *ctx, V9fsPath *oldpath,
                      V9fsPath *dirpath, const char *name)
{
    int ret;
    V9fsString newpath;
    char buffer[PATH_MAX], buffer1[PATH_MAX];

    v9fs_string_init(&newpath);
    v9fs_string_sprintf(&newpath, "%s/%s", dirpath->data, name);

    ret = link(rpath(ctx, oldpath->data, buffer),
               rpath(ctx, newpath.data, buffer1));
    v9fs_string_free(&newpath);
    return ret;
}

static int local_truncate(FsContext *ctx, V9fsPath *fs_path, off_t size)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    return truncate(rpath(ctx, path, buffer), size);
}

static int local_rename(FsContext *ctx, const char *oldpath,
                        const char *newpath)
{
    char buffer[PATH_MAX], buffer1[PATH_MAX];

    return rename(rpath(ctx, oldpath, buffer), rpath(ctx, newpath, buffer1));
}

static int local_chown(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    if ((credp->fc_uid == -1 && credp->fc_gid == -1) ||
            (fs_ctx->fs_sm == SM_PASSTHROUGH)) {
        return lchown(rpath(fs_ctx, path, buffer), credp->fc_uid,
                credp->fc_gid);
    } else if (fs_ctx->fs_sm == SM_MAPPED) {
        return local_set_xattr(rpath(fs_ctx, path, buffer), credp);
    } else if ((fs_ctx->fs_sm == SM_PASSTHROUGH) ||
               (fs_ctx->fs_sm == SM_NONE)) {
        return lchown(rpath(fs_ctx, path, buffer), credp->fc_uid,
                credp->fc_gid);
    }
    return -1;
}

static int local_utimensat(FsContext *s, V9fsPath *fs_path,
                           const struct timespec *buf)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    return qemu_utimensat(AT_FDCWD, rpath(s, path, buffer), buf,
                          AT_SYMLINK_NOFOLLOW);
}

static int local_remove(FsContext *ctx, const char *path)
{
    char buffer[PATH_MAX];
    return remove(rpath(ctx, path, buffer));
}

static int local_fsync(FsContext *ctx, int fd, int datasync)
{
    if (datasync) {
        return qemu_fdatasync(fd);
    } else {
        return fsync(fd);
    }
}

static int local_statfs(FsContext *s, V9fsPath *fs_path, struct statfs *stbuf)
{
    char buffer[PATH_MAX];
    char *path = fs_path->data;

    return statfs(rpath(s, path, buffer), stbuf);
}

static ssize_t local_lgetxattr(FsContext *ctx, V9fsPath *fs_path,
                               const char *name, void *value, size_t size)
{
    char *path = fs_path->data;

    return v9fs_get_xattr(ctx, path, name, value, size);
}

static ssize_t local_llistxattr(FsContext *ctx, V9fsPath *fs_path,
                                void *value, size_t size)
{
    char *path = fs_path->data;

    return v9fs_list_xattr(ctx, path, value, size);
}

static int local_lsetxattr(FsContext *ctx, V9fsPath *fs_path, const char *name,
                           void *value, size_t size, int flags)
{
    char *path = fs_path->data;

    return v9fs_set_xattr(ctx, path, name, value, size, flags);
}

static int local_lremovexattr(FsContext *ctx, V9fsPath *fs_path,
                              const char *name)
{
    char *path = fs_path->data;

    return v9fs_remove_xattr(ctx, path, name);
}

static int local_name_to_path(FsContext *ctx, V9fsPath *dir_path,
                              const char *name, V9fsPath *target)
{
    if (dir_path) {
        v9fs_string_sprintf((V9fsString *)target, "%s/%s",
                            dir_path->data, name);
    } else {
        v9fs_string_sprintf((V9fsString *)target, "%s", name);
    }
    /* Bump the size for including terminating NULL */
    target->size++;
    return 0;
}

static int local_renameat(FsContext *ctx, V9fsPath *olddir,
                          const char *old_name, V9fsPath *newdir,
                          const char *new_name)
{
    int ret;
    V9fsString old_full_name, new_full_name;

    v9fs_string_init(&old_full_name);
    v9fs_string_init(&new_full_name);

    v9fs_string_sprintf(&old_full_name, "%s/%s", olddir->data, old_name);
    v9fs_string_sprintf(&new_full_name, "%s/%s", newdir->data, new_name);

    ret = local_rename(ctx, old_full_name.data, new_full_name.data);
    v9fs_string_free(&old_full_name);
    v9fs_string_free(&new_full_name);
    return ret;
}

static int local_unlinkat(FsContext *ctx, V9fsPath *dir,
                          const char *name, int flags)
{
    int ret;
    V9fsString fullname;
    char buffer[PATH_MAX];
    v9fs_string_init(&fullname);

    v9fs_string_sprintf(&fullname, "%s/%s", dir->data, name);
    ret = remove(rpath(ctx, fullname.data, buffer));
    v9fs_string_free(&fullname);

    return ret;
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
    .readdir_r = local_readdir_r,
    .seekdir = local_seekdir,
    .preadv = local_preadv,
    .pwritev = local_pwritev,
    .chmod = local_chmod,
    .mknod = local_mknod,
    .mkdir = local_mkdir,
    .fstat = local_fstat,
    .open2 = local_open2,
    .symlink = local_symlink,
    .link = local_link,
    .truncate = local_truncate,
    .rename = local_rename,
    .chown = local_chown,
    .utimensat = local_utimensat,
    .remove = local_remove,
    .fsync = local_fsync,
    .statfs = local_statfs,
    .lgetxattr = local_lgetxattr,
    .llistxattr = local_llistxattr,
    .lsetxattr = local_lsetxattr,
    .lremovexattr = local_lremovexattr,
    .name_to_path = local_name_to_path,
    .renameat  = local_renameat,
    .unlinkat = local_unlinkat,
};
