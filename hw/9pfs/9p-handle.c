/*
 * 9p handle callback
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *    Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "9p.h"
#include "9p-xattr.h"
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "qemu/xattr.h"
#include "qemu/error-report.h"
#include <linux/fs.h>
#ifdef CONFIG_LINUX_MAGIC_H
#include <linux/magic.h>
#endif
#include <sys/ioctl.h>

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC  0x58465342
#endif
#ifndef EXT2_SUPER_MAGIC
#define EXT2_SUPER_MAGIC 0xEF53
#endif
#ifndef REISERFS_SUPER_MAGIC
#define REISERFS_SUPER_MAGIC 0x52654973
#endif
#ifndef BTRFS_SUPER_MAGIC
#define BTRFS_SUPER_MAGIC 0x9123683E
#endif

struct handle_data {
    int mountfd;
    int handle_bytes;
};

static inline int name_to_handle(int dirfd, const char *name,
                                 struct file_handle *fh, int *mnt_id, int flags)
{
    return name_to_handle_at(dirfd, name, fh, mnt_id, flags);
}

static inline int open_by_handle(int mountfd, const char *fh, int flags)
{
    return open_by_handle_at(mountfd, (struct file_handle *)fh, flags);
}

static int handle_update_file_cred(int dirfd, const char *name, FsCred *credp)
{
    int fd, ret;
    fd = openat(dirfd, name, O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) {
        return fd;
    }
    ret = fchownat(fd, "", credp->fc_uid, credp->fc_gid, AT_EMPTY_PATH);
    if (ret < 0) {
        goto err_out;
    }
    ret = fchmod(fd, credp->fc_mode & 07777);
err_out:
    close(fd);
    return ret;
}


static int handle_lstat(FsContext *fs_ctx, V9fsPath *fs_path,
                        struct stat *stbuf)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_PATH);
    if (fd < 0) {
        return fd;
    }
    ret = fstatat(fd, "", stbuf, AT_EMPTY_PATH);
    close(fd);
    return ret;
}

static ssize_t handle_readlink(FsContext *fs_ctx, V9fsPath *fs_path,
                               char *buf, size_t bufsz)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_PATH);
    if (fd < 0) {
        return fd;
    }
    ret = readlinkat(fd, "", buf, bufsz);
    close(fd);
    return ret;
}

static int handle_close(FsContext *ctx, V9fsFidOpenState *fs)
{
    return close(fs->fd);
}

static int handle_closedir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return closedir(fs->dir);
}

static int handle_open(FsContext *ctx, V9fsPath *fs_path,
                       int flags, V9fsFidOpenState *fs)
{
    struct handle_data *data = (struct handle_data *)ctx->private;

    fs->fd = open_by_handle(data->mountfd, fs_path->data, flags);
    return fs->fd;
}

static int handle_opendir(FsContext *ctx,
                          V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    int ret;
    ret = handle_open(ctx, fs_path, O_DIRECTORY, fs);
    if (ret < 0) {
        return -1;
    }
    fs->dir = fdopendir(ret);
    if (!fs->dir) {
        return -1;
    }
    return 0;
}

static void handle_rewinddir(FsContext *ctx, V9fsFidOpenState *fs)
{
    rewinddir(fs->dir);
}

static off_t handle_telldir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return telldir(fs->dir);
}

static int handle_readdir_r(FsContext *ctx, V9fsFidOpenState *fs,
                            struct dirent *entry,
                            struct dirent **result)
{
    return readdir_r(fs->dir, entry, result);
}

static void handle_seekdir(FsContext *ctx, V9fsFidOpenState *fs, off_t off)
{
    seekdir(fs->dir, off);
}

static ssize_t handle_preadv(FsContext *ctx, V9fsFidOpenState *fs,
                             const struct iovec *iov,
                             int iovcnt, off_t offset)
{
#ifdef CONFIG_PREADV
    return preadv(fs->fd, iov, iovcnt, offset);
#else
    int err = lseek(fs->fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        return readv(fs->fd, iov, iovcnt);
    }
#endif
}

static ssize_t handle_pwritev(FsContext *ctx, V9fsFidOpenState *fs,
                              const struct iovec *iov,
                              int iovcnt, off_t offset)
{
    ssize_t ret;
#ifdef CONFIG_PREADV
    ret = pwritev(fs->fd, iov, iovcnt, offset);
#else
    int err = lseek(fs->fd, offset, SEEK_SET);
    if (err == -1) {
        return err;
    } else {
        ret = writev(fs->fd, iov, iovcnt);
    }
#endif
#ifdef CONFIG_SYNC_FILE_RANGE
    if (ret > 0 && ctx->export_flags & V9FS_IMMEDIATE_WRITEOUT) {
        /*
         * Initiate a writeback. This is not a data integrity sync.
         * We want to ensure that we don't leave dirty pages in the cache
         * after write when writeout=immediate is sepcified.
         */
        sync_file_range(fs->fd, offset, ret,
                        SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE);
    }
#endif
    return ret;
}

static int handle_chmod(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = fchmod(fd, credp->fc_mode);
    close(fd);
    return ret;
}

static int handle_mknod(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    int dirfd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    dirfd = open_by_handle(data->mountfd, dir_path->data, O_PATH);
    if (dirfd < 0) {
        return dirfd;
    }
    ret = mknodat(dirfd, name, credp->fc_mode, credp->fc_rdev);
    if (!ret) {
        ret = handle_update_file_cred(dirfd, name, credp);
    }
    close(dirfd);
    return ret;
}

static int handle_mkdir(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    int dirfd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    dirfd = open_by_handle(data->mountfd, dir_path->data, O_PATH);
    if (dirfd < 0) {
        return dirfd;
    }
    ret = mkdirat(dirfd, name, credp->fc_mode);
    if (!ret) {
        ret = handle_update_file_cred(dirfd, name, credp);
    }
    close(dirfd);
    return ret;
}

static int handle_fstat(FsContext *fs_ctx, int fid_type,
                        V9fsFidOpenState *fs, struct stat *stbuf)
{
    int fd;

    if (fid_type == P9_FID_DIR) {
        fd = dirfd(fs->dir);
    } else {
        fd = fs->fd;
    }
    return fstat(fd, stbuf);
}

static int handle_open2(FsContext *fs_ctx, V9fsPath *dir_path, const char *name,
                        int flags, FsCred *credp, V9fsFidOpenState *fs)
{
    int ret;
    int dirfd, fd;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    dirfd = open_by_handle(data->mountfd, dir_path->data, O_PATH);
    if (dirfd < 0) {
        return dirfd;
    }
    fd = openat(dirfd, name, flags | O_NOFOLLOW, credp->fc_mode);
    if (fd >= 0) {
        ret = handle_update_file_cred(dirfd, name, credp);
        if (ret < 0) {
            close(fd);
            fd = ret;
        } else {
            fs->fd = fd;
        }
    }
    close(dirfd);
    return fd;
}


static int handle_symlink(FsContext *fs_ctx, const char *oldpath,
                          V9fsPath *dir_path, const char *name, FsCred *credp)
{
    int fd, dirfd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    dirfd = open_by_handle(data->mountfd, dir_path->data, O_PATH);
    if (dirfd < 0) {
        return dirfd;
    }
    ret = symlinkat(oldpath, dirfd, name);
    if (!ret) {
        fd = openat(dirfd, name, O_PATH | O_NOFOLLOW);
        if (fd < 0) {
            ret = fd;
            goto err_out;
        }
        ret = fchownat(fd, "", credp->fc_uid, credp->fc_gid, AT_EMPTY_PATH);
        close(fd);
    }
err_out:
    close(dirfd);
    return ret;
}

static int handle_link(FsContext *ctx, V9fsPath *oldpath,
                       V9fsPath *dirpath, const char *name)
{
    int oldfd, newdirfd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    oldfd = open_by_handle(data->mountfd, oldpath->data, O_PATH);
    if (oldfd < 0) {
        return oldfd;
    }
    newdirfd = open_by_handle(data->mountfd, dirpath->data, O_PATH);
    if (newdirfd < 0) {
        close(oldfd);
        return newdirfd;
    }
    ret = linkat(oldfd, "", newdirfd, name, AT_EMPTY_PATH);
    close(newdirfd);
    close(oldfd);
    return ret;
}

static int handle_truncate(FsContext *ctx, V9fsPath *fs_path, off_t size)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK | O_WRONLY);
    if (fd < 0) {
        return fd;
    }
    ret = ftruncate(fd, size);
    close(fd);
    return ret;
}

static int handle_rename(FsContext *ctx, const char *oldpath,
                         const char *newpath)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int handle_chown(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)fs_ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_PATH);
    if (fd < 0) {
        return fd;
    }
    ret = fchownat(fd, "", credp->fc_uid, credp->fc_gid, AT_EMPTY_PATH);
    close(fd);
    return ret;
}

static int handle_utimensat(FsContext *ctx, V9fsPath *fs_path,
                            const struct timespec *buf)
{
    int ret;
#ifdef CONFIG_UTIMENSAT
    int fd;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = futimens(fd, buf);
    close(fd);
#else
    ret = -1;
    errno = ENOSYS;
#endif
    return ret;
}

static int handle_remove(FsContext *ctx, const char *path)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int handle_fsync(FsContext *ctx, int fid_type,
                        V9fsFidOpenState *fs, int datasync)
{
    int fd;

    if (fid_type == P9_FID_DIR) {
        fd = dirfd(fs->dir);
    } else {
        fd = fs->fd;
    }

    if (datasync) {
        return qemu_fdatasync(fd);
    } else {
        return fsync(fd);
    }
}

static int handle_statfs(FsContext *ctx, V9fsPath *fs_path,
                         struct statfs *stbuf)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = fstatfs(fd, stbuf);
    close(fd);
    return ret;
}

static ssize_t handle_lgetxattr(FsContext *ctx, V9fsPath *fs_path,
                                const char *name, void *value, size_t size)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = fgetxattr(fd, name, value, size);
    close(fd);
    return ret;
}

static ssize_t handle_llistxattr(FsContext *ctx, V9fsPath *fs_path,
                                 void *value, size_t size)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = flistxattr(fd, value, size);
    close(fd);
    return ret;
}

static int handle_lsetxattr(FsContext *ctx, V9fsPath *fs_path, const char *name,
                            void *value, size_t size, int flags)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = fsetxattr(fd, name, value, size, flags);
    close(fd);
    return ret;
}

static int handle_lremovexattr(FsContext *ctx, V9fsPath *fs_path,
                               const char *name)
{
    int fd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    fd = open_by_handle(data->mountfd, fs_path->data, O_NONBLOCK);
    if (fd < 0) {
        return fd;
    }
    ret = fremovexattr(fd, name);
    close(fd);
    return ret;
}

static int handle_name_to_path(FsContext *ctx, V9fsPath *dir_path,
                              const char *name, V9fsPath *target)
{
    char *buffer;
    struct file_handle *fh;
    int dirfd, ret, mnt_id;
    struct handle_data *data = (struct handle_data *)ctx->private;

    /* "." and ".." are not allowed */
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        errno = EINVAL;
        return -1;

    }
    if (dir_path) {
        dirfd = open_by_handle(data->mountfd, dir_path->data, O_PATH);
    } else {
        /* relative to export root */
        buffer = rpath(ctx, ".");
        dirfd = open(buffer, O_DIRECTORY);
        g_free(buffer);
    }
    if (dirfd < 0) {
        return dirfd;
    }
    fh = g_malloc(sizeof(struct file_handle) + data->handle_bytes);
    fh->handle_bytes = data->handle_bytes;
    /* add a "./" at the beginning of the path */
    buffer = g_strdup_printf("./%s", name);
    /* flag = 0 imply don't follow symlink */
    ret = name_to_handle(dirfd, buffer, fh, &mnt_id, 0);
    if (!ret) {
        target->data = (char *)fh;
        target->size = sizeof(struct file_handle) + data->handle_bytes;
    } else {
        g_free(fh);
    }
    close(dirfd);
    g_free(buffer);
    return ret;
}

static int handle_renameat(FsContext *ctx, V9fsPath *olddir,
                           const char *old_name, V9fsPath *newdir,
                           const char *new_name)
{
    int olddirfd, newdirfd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;

    olddirfd = open_by_handle(data->mountfd, olddir->data, O_PATH);
    if (olddirfd < 0) {
        return olddirfd;
    }
    newdirfd = open_by_handle(data->mountfd, newdir->data, O_PATH);
    if (newdirfd < 0) {
        close(olddirfd);
        return newdirfd;
    }
    ret = renameat(olddirfd, old_name, newdirfd, new_name);
    close(newdirfd);
    close(olddirfd);
    return ret;
}

static int handle_unlinkat(FsContext *ctx, V9fsPath *dir,
                           const char *name, int flags)
{
    int dirfd, ret;
    struct handle_data *data = (struct handle_data *)ctx->private;
    int rflags;

    dirfd = open_by_handle(data->mountfd, dir->data, O_PATH);
    if (dirfd < 0) {
        return dirfd;
    }

    rflags = 0;
    if (flags & P9_DOTL_AT_REMOVEDIR) {
        rflags |= AT_REMOVEDIR;
    }

    ret = unlinkat(dirfd, name, rflags);

    close(dirfd);
    return ret;
}

static int handle_ioc_getversion(FsContext *ctx, V9fsPath *path,
                                 mode_t st_mode, uint64_t *st_gen)
{
#ifdef FS_IOC_GETVERSION
    int err;
    V9fsFidOpenState fid_open;

    /*
     * Do not try to open special files like device nodes, fifos etc
     * We can get fd for regular files and directories only
     */
    if (!S_ISREG(st_mode) && !S_ISDIR(st_mode)) {
        errno = ENOTTY;
        return -1;
    }
    err = handle_open(ctx, path, O_RDONLY, &fid_open);
    if (err < 0) {
        return err;
    }
    err = ioctl(fid_open.fd, FS_IOC_GETVERSION, st_gen);
    handle_close(ctx, &fid_open);
    return err;
#else
    errno = ENOTTY;
    return -1;
#endif
}

static int handle_init(FsContext *ctx)
{
    int ret, mnt_id;
    struct statfs stbuf;
    struct file_handle fh;
    struct handle_data *data = g_malloc(sizeof(struct handle_data));

    data->mountfd = open(ctx->fs_root, O_DIRECTORY);
    if (data->mountfd < 0) {
        ret = data->mountfd;
        goto err_out;
    }
    ret = statfs(ctx->fs_root, &stbuf);
    if (!ret) {
        switch (stbuf.f_type) {
        case EXT2_SUPER_MAGIC:
        case BTRFS_SUPER_MAGIC:
        case REISERFS_SUPER_MAGIC:
        case XFS_SUPER_MAGIC:
            ctx->exops.get_st_gen = handle_ioc_getversion;
            break;
        }
    }
    memset(&fh, 0, sizeof(struct file_handle));
    ret = name_to_handle(data->mountfd, ".", &fh, &mnt_id, 0);
    if (ret && errno == EOVERFLOW) {
        data->handle_bytes = fh.handle_bytes;
        ctx->private = data;
        ret = 0;
        goto out;
    }
    /* we got 0 byte handle ? */
    ret = -1;
    close(data->mountfd);
err_out:
    g_free(data);
out:
    return ret;
}

static int handle_parse_opts(QemuOpts *opts, struct FsDriverEntry *fse)
{
    const char *sec_model = qemu_opt_get(opts, "security_model");
    const char *path = qemu_opt_get(opts, "path");

    if (sec_model) {
        error_report("Invalid argument security_model specified with handle fsdriver");
        return -1;
    }

    if (!path) {
        error_report("fsdev: No path specified");
        return -1;
    }
    fse->path = g_strdup(path);
    return 0;

}

FileOperations handle_ops = {
    .parse_opts   = handle_parse_opts,
    .init         = handle_init,
    .lstat        = handle_lstat,
    .readlink     = handle_readlink,
    .close        = handle_close,
    .closedir     = handle_closedir,
    .open         = handle_open,
    .opendir      = handle_opendir,
    .rewinddir    = handle_rewinddir,
    .telldir      = handle_telldir,
    .readdir_r    = handle_readdir_r,
    .seekdir      = handle_seekdir,
    .preadv       = handle_preadv,
    .pwritev      = handle_pwritev,
    .chmod        = handle_chmod,
    .mknod        = handle_mknod,
    .mkdir        = handle_mkdir,
    .fstat        = handle_fstat,
    .open2        = handle_open2,
    .symlink      = handle_symlink,
    .link         = handle_link,
    .truncate     = handle_truncate,
    .rename       = handle_rename,
    .chown        = handle_chown,
    .utimensat    = handle_utimensat,
    .remove       = handle_remove,
    .fsync        = handle_fsync,
    .statfs       = handle_statfs,
    .lgetxattr    = handle_lgetxattr,
    .llistxattr   = handle_llistxattr,
    .lsetxattr    = handle_lsetxattr,
    .lremovexattr = handle_lremovexattr,
    .name_to_path = handle_name_to_path,
    .renameat     = handle_renameat,
    .unlinkat     = handle_unlinkat,
};
