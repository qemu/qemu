/*
 * Virtio 9p Proxy callback
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 * M. Mohan Kumar <mohan@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#include <sys/socket.h>
#include <sys/un.h>
#include "hw/virtio.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-proxy.h"

typedef struct V9fsProxy {
    int sockfd;
    QemuMutex mutex;
    struct iovec iovec;
} V9fsProxy;

static int proxy_lstat(FsContext *fs_ctx, V9fsPath *fs_path, struct stat *stbuf)
{
    errno = EOPNOTSUPP;
    return -1;
}

static ssize_t proxy_readlink(FsContext *fs_ctx, V9fsPath *fs_path,
                              char *buf, size_t bufsz)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_close(FsContext *ctx, V9fsFidOpenState *fs)
{
    return close(fs->fd);
}

static int proxy_closedir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return closedir(fs->dir);
}

static int proxy_open(FsContext *ctx, V9fsPath *fs_path,
                      int flags, V9fsFidOpenState *fs)
{
    fs->fd = -1;
    return fs->fd;
}

static int proxy_opendir(FsContext *ctx,
                         V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    fs->dir = NULL;
    errno = EOPNOTSUPP;
    return -1;
}

static void proxy_rewinddir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return rewinddir(fs->dir);
}

static off_t proxy_telldir(FsContext *ctx, V9fsFidOpenState *fs)
{
    return telldir(fs->dir);
}

static int proxy_readdir_r(FsContext *ctx, V9fsFidOpenState *fs,
                           struct dirent *entry,
                           struct dirent **result)
{
    return readdir_r(fs->dir, entry, result);
}

static void proxy_seekdir(FsContext *ctx, V9fsFidOpenState *fs, off_t off)
{
    return seekdir(fs->dir, off);
}

static ssize_t proxy_preadv(FsContext *ctx, V9fsFidOpenState *fs,
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

static ssize_t proxy_pwritev(FsContext *ctx, V9fsFidOpenState *fs,
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

static int proxy_chmod(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_mknod(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_mkdir(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_fstat(FsContext *fs_ctx, int fid_type,
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

static int proxy_open2(FsContext *fs_ctx, V9fsPath *dir_path, const char *name,
                       int flags, FsCred *credp, V9fsFidOpenState *fs)
{
    fs->fd = -1;
    errno = EOPNOTSUPP;
    return -1;
}


static int proxy_symlink(FsContext *fs_ctx, const char *oldpath,
                         V9fsPath *dir_path, const char *name, FsCred *credp)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_link(FsContext *ctx, V9fsPath *oldpath,
                      V9fsPath *dirpath, const char *name)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_truncate(FsContext *ctx, V9fsPath *fs_path, off_t size)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_rename(FsContext *ctx, const char *oldpath,
                        const char *newpath)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_chown(FsContext *fs_ctx, V9fsPath *fs_path, FsCred *credp)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_utimensat(FsContext *s, V9fsPath *fs_path,
                           const struct timespec *buf)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_remove(FsContext *ctx, const char *path)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_fsync(FsContext *ctx, int fid_type,
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

static int proxy_statfs(FsContext *s, V9fsPath *fs_path, struct statfs *stbuf)
{
    errno = EOPNOTSUPP;
    return -1;
}

static ssize_t proxy_lgetxattr(FsContext *ctx, V9fsPath *fs_path,
                               const char *name, void *value, size_t size)
{
    errno = EOPNOTSUPP;
    return -1;
}

static ssize_t proxy_llistxattr(FsContext *ctx, V9fsPath *fs_path,
                                void *value, size_t size)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_lsetxattr(FsContext *ctx, V9fsPath *fs_path, const char *name,
                           void *value, size_t size, int flags)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_lremovexattr(FsContext *ctx, V9fsPath *fs_path,
                              const char *name)
{
    errno = EOPNOTSUPP;
    return -1;
}

static int proxy_name_to_path(FsContext *ctx, V9fsPath *dir_path,
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

static int proxy_renameat(FsContext *ctx, V9fsPath *olddir,
                          const char *old_name, V9fsPath *newdir,
                          const char *new_name)
{
    int ret;
    V9fsString old_full_name, new_full_name;

    v9fs_string_init(&old_full_name);
    v9fs_string_init(&new_full_name);

    v9fs_string_sprintf(&old_full_name, "%s/%s", olddir->data, old_name);
    v9fs_string_sprintf(&new_full_name, "%s/%s", newdir->data, new_name);

    ret = proxy_rename(ctx, old_full_name.data, new_full_name.data);
    v9fs_string_free(&old_full_name);
    v9fs_string_free(&new_full_name);
    return ret;
}

static int proxy_unlinkat(FsContext *ctx, V9fsPath *dir,
                          const char *name, int flags)
{
    int ret;
    V9fsString fullname;
    v9fs_string_init(&fullname);

    v9fs_string_sprintf(&fullname, "%s/%s", dir->data, name);
    ret = proxy_remove(ctx, fullname.data);
    v9fs_string_free(&fullname);

    return ret;
}

static int proxy_parse_opts(QemuOpts *opts, struct FsDriverEntry *fs)
{
    const char *sock_fd = qemu_opt_get(opts, "sock_fd");

    if (sock_fd) {
        fprintf(stderr, "sock_fd option not specified\n");
        return -1;
    }
    fs->path = g_strdup(sock_fd);
    return 0;
}

static int proxy_init(FsContext *ctx)
{
    V9fsProxy *proxy = g_malloc(sizeof(V9fsProxy));
    int sock_id;

    sock_id = atoi(ctx->fs_root);
    if (sock_id < 0) {
        fprintf(stderr, "socket descriptor not initialized\n");
        return -1;
    }
    g_free(ctx->fs_root);

    proxy->iovec.iov_base = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    proxy->iovec.iov_len = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;
    ctx->private = proxy;
    proxy->sockfd = sock_id;
    qemu_mutex_init(&proxy->mutex);

    ctx->export_flags |= V9FS_PATHNAME_FSCONTEXT;
    return 0;
}

FileOperations proxy_ops = {
    .parse_opts   = proxy_parse_opts,
    .init         = proxy_init,
    .lstat        = proxy_lstat,
    .readlink     = proxy_readlink,
    .close        = proxy_close,
    .closedir     = proxy_closedir,
    .open         = proxy_open,
    .opendir      = proxy_opendir,
    .rewinddir    = proxy_rewinddir,
    .telldir      = proxy_telldir,
    .readdir_r    = proxy_readdir_r,
    .seekdir      = proxy_seekdir,
    .preadv       = proxy_preadv,
    .pwritev      = proxy_pwritev,
    .chmod        = proxy_chmod,
    .mknod        = proxy_mknod,
    .mkdir        = proxy_mkdir,
    .fstat        = proxy_fstat,
    .open2        = proxy_open2,
    .symlink      = proxy_symlink,
    .link         = proxy_link,
    .truncate     = proxy_truncate,
    .rename       = proxy_rename,
    .chown        = proxy_chown,
    .utimensat    = proxy_utimensat,
    .remove       = proxy_remove,
    .fsync        = proxy_fsync,
    .statfs       = proxy_statfs,
    .lgetxattr    = proxy_lgetxattr,
    .llistxattr   = proxy_llistxattr,
    .lsetxattr    = proxy_lsetxattr,
    .lremovexattr = proxy_lremovexattr,
    .name_to_path = proxy_name_to_path,
    .renameat     = proxy_renameat,
    .unlinkat     = proxy_unlinkat,
};
