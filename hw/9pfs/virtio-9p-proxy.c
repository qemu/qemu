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
    struct iovec in_iovec;
    struct iovec out_iovec;
} V9fsProxy;

/*
 * Return received file descriptor on success in *status.
 * errno is also returned on *status (which will be < 0)
 * return < 0 on transport error.
 */
static int v9fs_receivefd(int sockfd, int *status)
{
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    int retval, data, fd;
    union MsgControl msg_control;

    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &msg_control;
    msg.msg_controllen = sizeof(msg_control);

    do {
        retval = recvmsg(sockfd, &msg, 0);
    } while (retval < 0 && errno == EINTR);
    if (retval <= 0) {
        return retval;
    }
    /*
     * data is set to V9FS_FD_VALID, if ancillary data is sent.  If this
     * request doesn't need ancillary data (fd) or an error occurred,
     * data is set to negative errno value.
     */
    if (data != V9FS_FD_VALID) {
        *status = data;
        return 0;
    }
    /*
     * File descriptor (fd) is sent in the ancillary data. Check if we
     * indeed received it. One of the reasons to fail to receive it is if
     * we exceeded the maximum number of file descriptors!
     */
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        fd = *((int *)CMSG_DATA(cmsg));
        *status = fd;
        return 0;
    }
    *status = -ENFILE;  /* Ancillary data sent but not received */
    return 0;
}

static ssize_t socket_read(int sockfd, void *buff, size_t size)
{
    ssize_t retval, total = 0;

    while (size) {
        retval = read(sockfd, buff, size);
        if (retval == 0) {
            return -EIO;
        }
        if (retval < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        size -= retval;
        buff += retval;
        total += retval;
    }
    return total;
}

/*
 * return < 0 on transport error.
 * *status is valid only if return >= 0
 */
static int v9fs_receive_status(V9fsProxy *proxy,
                               struct iovec *reply, int *status)
{
    int retval;
    ProxyHeader header;

    *status = 0;
    reply->iov_len = 0;
    retval = socket_read(proxy->sockfd, reply->iov_base, PROXY_HDR_SZ);
    if (retval < 0) {
        return retval;
    }
    reply->iov_len = PROXY_HDR_SZ;
    proxy_unmarshal(reply, 0, "dd", &header.type, &header.size);
    if (header.size != sizeof(int)) {
        *status = -ENOBUFS;
        return 0;
    }
    retval = socket_read(proxy->sockfd,
                         reply->iov_base + PROXY_HDR_SZ, header.size);
    if (retval < 0) {
        return retval;
    }
    reply->iov_len += header.size;
    proxy_unmarshal(reply, PROXY_HDR_SZ, "d", status);
    return 0;
}

/*
 * Proxy->header and proxy->request written to socket by QEMU process.
 * This request read by proxy helper process
 * returns 0 on success and -errno on error
 */
static int v9fs_request(V9fsProxy *proxy, int type,
                        void *response, const char *fmt, ...)
{
    dev_t rdev;
    va_list ap;
    int retval = 0;
    ProxyHeader header = { 0, 0};
    int flags, mode, uid, gid;
    V9fsString *path, *oldpath;
    struct iovec *iovec = NULL, *reply = NULL;

    qemu_mutex_lock(&proxy->mutex);

    if (proxy->sockfd == -1) {
        retval = -EIO;
        goto err_out;
    }
    iovec = &proxy->out_iovec;
    reply = &proxy->in_iovec;
    va_start(ap, fmt);
    switch (type) {
    case T_OPEN:
        path = va_arg(ap, V9fsString *);
        flags = va_arg(ap, int);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "sd", path, flags);
        if (retval > 0) {
            header.size = retval;
            header.type = T_OPEN;
        }
        break;
    case T_CREATE:
        path = va_arg(ap, V9fsString *);
        flags = va_arg(ap, int);
        mode = va_arg(ap, int);
        uid = va_arg(ap, int);
        gid = va_arg(ap, int);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "sdddd", path,
                                    flags, mode, uid, gid);
        if (retval > 0) {
            header.size = retval;
            header.type = T_CREATE;
        }
        break;
    case T_MKNOD:
        path = va_arg(ap, V9fsString *);
        mode = va_arg(ap, int);
        rdev = va_arg(ap, long int);
        uid = va_arg(ap, int);
        gid = va_arg(ap, int);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "ddsdq",
                                    uid, gid, path, mode, rdev);
        if (retval > 0) {
            header.size = retval;
            header.type = T_MKNOD;
        }
        break;
    case T_MKDIR:
        path = va_arg(ap, V9fsString *);
        mode = va_arg(ap, int);
        uid = va_arg(ap, int);
        gid = va_arg(ap, int);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "ddsd",
                                    uid, gid, path, mode);
        if (retval > 0) {
            header.size = retval;
            header.type = T_MKDIR;
        }
        break;
    case T_SYMLINK:
        oldpath = va_arg(ap, V9fsString *);
        path = va_arg(ap, V9fsString *);
        uid = va_arg(ap, int);
        gid = va_arg(ap, int);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "ddss",
                                    uid, gid, oldpath, path);
        if (retval > 0) {
            header.size = retval;
            header.type = T_SYMLINK;
        }
        break;
    case T_LINK:
        oldpath = va_arg(ap, V9fsString *);
        path = va_arg(ap, V9fsString *);
        retval = proxy_marshal(iovec, PROXY_HDR_SZ, "ss",
                                    oldpath, path);
        if (retval > 0) {
            header.size = retval;
            header.type = T_LINK;
        }
        break;
    default:
        error_report("Invalid type %d\n", type);
        retval = -EINVAL;
        break;
    }
    va_end(ap);

    if (retval < 0) {
        goto err_out;
    }

    /* marshal the header details */
    proxy_marshal(iovec, 0, "dd", header.type, header.size);
    header.size += PROXY_HDR_SZ;

    retval = qemu_write_full(proxy->sockfd, iovec->iov_base, header.size);
    if (retval != header.size) {
        goto close_error;
    }

    switch (type) {
    case T_OPEN:
    case T_CREATE:
        /*
         * A file descriptor is returned as response for
         * T_OPEN,T_CREATE on success
         */
        if (v9fs_receivefd(proxy->sockfd, &retval) < 0) {
            goto close_error;
        }
        break;
    case T_MKNOD:
    case T_MKDIR:
    case T_SYMLINK:
    case T_LINK:
        if (v9fs_receive_status(proxy, reply, &retval) < 0) {
            goto close_error;
        }
        break;
    }

err_out:
    qemu_mutex_unlock(&proxy->mutex);
    return retval;

close_error:
    close(proxy->sockfd);
    proxy->sockfd = -1;
    qemu_mutex_unlock(&proxy->mutex);
    return -EIO;
}

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
    fs->fd = v9fs_request(ctx->private, T_OPEN, NULL, "sd", fs_path, flags);
    if (fs->fd < 0) {
        errno = -fs->fd;
        fs->fd = -1;
    }
    return fs->fd;
}

static int proxy_opendir(FsContext *ctx,
                         V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    int serrno, fd;

    fs->dir = NULL;
    fd = v9fs_request(ctx->private, T_OPEN, NULL, "sd", fs_path, O_DIRECTORY);
    if (fd < 0) {
        errno = -fd;
        return -1;
    }
    fs->dir = fdopendir(fd);
    if (!fs->dir) {
        serrno = errno;
        close(fd);
        errno = serrno;
        return -1;
    }
    return 0;
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
    int retval;
    V9fsString fullname;

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);

    retval = v9fs_request(fs_ctx->private, T_MKNOD, NULL, "sdqdd",
                          &fullname, credp->fc_mode, credp->fc_rdev,
                          credp->fc_uid, credp->fc_gid);
    v9fs_string_free(&fullname);
    if (retval < 0) {
        errno = -retval;
        retval = -1;
    }
    return retval;
}

static int proxy_mkdir(FsContext *fs_ctx, V9fsPath *dir_path,
                       const char *name, FsCred *credp)
{
    int retval;
    V9fsString fullname;

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);

    retval = v9fs_request(fs_ctx->private, T_MKDIR, NULL, "sddd", &fullname,
                          credp->fc_mode, credp->fc_uid, credp->fc_gid);
    v9fs_string_free(&fullname);
    if (retval < 0) {
        errno = -retval;
        retval = -1;
    }
    v9fs_string_free(&fullname);
    return retval;
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
    V9fsString fullname;

    v9fs_string_init(&fullname);
    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);

    fs->fd = v9fs_request(fs_ctx->private, T_CREATE, NULL, "sdddd",
                          &fullname, flags, credp->fc_mode,
                          credp->fc_uid, credp->fc_gid);
    v9fs_string_free(&fullname);
    if (fs->fd < 0) {
        errno = -fs->fd;
        fs->fd = -1;
    }
    return fs->fd;
}

static int proxy_symlink(FsContext *fs_ctx, const char *oldpath,
                         V9fsPath *dir_path, const char *name, FsCred *credp)
{
    int retval;
    V9fsString fullname, target;

    v9fs_string_init(&fullname);
    v9fs_string_init(&target);

    v9fs_string_sprintf(&fullname, "%s/%s", dir_path->data, name);
    v9fs_string_sprintf(&target, "%s", oldpath);

    retval = v9fs_request(fs_ctx->private, T_SYMLINK, NULL, "ssdd",
                          &target, &fullname, credp->fc_uid, credp->fc_gid);
    v9fs_string_free(&fullname);
    v9fs_string_free(&target);
    if (retval < 0) {
        errno = -retval;
        retval = -1;
    }
    return retval;
}

static int proxy_link(FsContext *ctx, V9fsPath *oldpath,
                      V9fsPath *dirpath, const char *name)
{
    int retval;
    V9fsString newpath;

    v9fs_string_init(&newpath);
    v9fs_string_sprintf(&newpath, "%s/%s", dirpath->data, name);

    retval = v9fs_request(ctx->private, T_LINK, NULL, "ss",
                          oldpath, &newpath);
    v9fs_string_free(&newpath);
    if (retval < 0) {
        errno = -retval;
        retval = -1;
    }
    return retval;
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

    proxy->in_iovec.iov_base  = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    proxy->in_iovec.iov_len   = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;
    proxy->out_iovec.iov_base = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    proxy->out_iovec.iov_len  = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;

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
