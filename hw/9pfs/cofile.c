
/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "fsdev/qemu-fsdev.h"
#include "qemu-thread.h"
#include "qemu-coroutine.h"
#include "virtio-9p-coth.h"

int v9fs_co_lstat(V9fsState *s, V9fsString *path, struct stat *stbuf)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lstat(&s->ctx, path->data, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_fstat(V9fsState *s, int fd, struct stat *stbuf)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->fstat(&s->ctx, fd, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_open(V9fsState *s, V9fsFidState *fidp, int flags)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            fidp->fs.fd = s->ops->open(&s->ctx, fidp->path.data, flags);
            if (fidp->fs.fd == -1) {
                err = -errno;
            } else {
                err = 0;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    if (!err) {
        total_open_fd++;
        if (total_open_fd > open_fd_hw) {
            v9fs_reclaim_fd(s);
        }
    }
    return err;
}

int v9fs_co_open2(V9fsState *s, V9fsFidState *fidp, V9fsString *name, gid_t gid,
                  int flags, int mode, struct stat *stbuf)
{
    int err;
    FsCred cred;
    V9fsString fullname;

    cred_init(&cred);
    cred.fc_mode = mode & 07777;
    cred.fc_uid = fidp->uid;
    cred.fc_gid = gid;
    v9fs_string_init(&fullname);
    /*
     * Hold the directory fid lock so that directory path name
     * don't change. Read lock is fine because this fid cannot
     * be used by any other operation.
     */
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name->data);
    v9fs_co_run_in_worker(
        {
            fidp->fs.fd = s->ops->open2(&s->ctx, fullname.data, flags, &cred);
            if (fidp->fs.fd == -1) {
                err = -errno;
            } else {
                err = s->ops->lstat(&s->ctx, fullname.data, stbuf);
                if (err < 0) {
                    err = -errno;
                    err = s->ops->close(&s->ctx, fidp->fs.fd);
                } else {
                    v9fs_string_copy(&fidp->path, &fullname);
                }
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    if (!err) {
        total_open_fd++;
        if (total_open_fd > open_fd_hw) {
            v9fs_reclaim_fd(s);
        }
    }
    v9fs_string_free(&fullname);
    return err;
}

int v9fs_co_close(V9fsState *s, int fd)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->close(&s->ctx, fd);
            if (err < 0) {
                err = -errno;
            }
        });
    if (!err) {
        total_open_fd--;
    }
    return err;
}

int v9fs_co_fsync(V9fsState *s, V9fsFidState *fidp, int datasync)
{
    int fd;
    int err;

    fd = fidp->fs.fd;
    v9fs_co_run_in_worker(
        {
            err = s->ops->fsync(&s->ctx, fd, datasync);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_link(V9fsState *s, V9fsString *oldpath, V9fsString *newpath)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->link(&s->ctx, oldpath->data, newpath->data);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_pwritev(V9fsState *s, V9fsFidState *fidp,
                    struct iovec *iov, int iovcnt, int64_t offset)
{
    int fd;
    int err;

    fd = fidp->fs.fd;
    v9fs_co_run_in_worker(
        {
            err = s->ops->pwritev(&s->ctx, fd, iov, iovcnt, offset);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_preadv(V9fsState *s, V9fsFidState *fidp,
                   struct iovec *iov, int iovcnt, int64_t offset)
{
    int fd;
    int err;

    fd = fidp->fs.fd;
    v9fs_co_run_in_worker(
        {
            err = s->ops->preadv(&s->ctx, fd, iov, iovcnt, offset);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}
