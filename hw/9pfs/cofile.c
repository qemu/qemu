
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

int v9fs_co_lstat(V9fsPDU *pdu, V9fsPath *path, struct stat *stbuf)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lstat(&s->ctx, path, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int v9fs_co_fstat(V9fsPDU *pdu, int fd, struct stat *stbuf)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_co_run_in_worker(
        {
            err = s->ops->fstat(&s->ctx, fd, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_open(V9fsPDU *pdu, V9fsFidState *fidp, int flags)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            fidp->fs.fd = s->ops->open(&s->ctx, &fidp->path, flags);
            if (fidp->fs.fd == -1) {
                err = -errno;
            } else {
                err = 0;
            }
        });
    v9fs_path_unlock(s);
    if (!err) {
        total_open_fd++;
        if (total_open_fd > open_fd_hw) {
            v9fs_reclaim_fd(pdu);
        }
    }
    return err;
}

int v9fs_co_open2(V9fsPDU *pdu, V9fsFidState *fidp, V9fsString *name, gid_t gid,
                  int flags, int mode, struct stat *stbuf)
{
    int err;
    FsCred cred;
    V9fsPath path;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    cred_init(&cred);
    cred.fc_mode = mode & 07777;
    cred.fc_uid = fidp->uid;
    cred.fc_gid = gid;
    /*
     * Hold the directory fid lock so that directory path name
     * don't change. Read lock is fine because this fid cannot
     * be used by any other operation.
     */
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            fidp->fs.fd = s->ops->open2(&s->ctx, &fidp->path,
                                        name->data, flags, &cred);
            if (fidp->fs.fd == -1) {
                err = -errno;
            } else {
                v9fs_path_init(&path);
                err = v9fs_name_to_path(s, &fidp->path, name->data, &path);
                if (!err) {
                    err = s->ops->lstat(&s->ctx, &path, stbuf);
                    if (err < 0) {
                        err = -errno;
                        s->ops->close(&s->ctx, fidp->fs.fd);
                    } else {
                        v9fs_path_copy(&fidp->path, &path);
                    }
                } else {
                    s->ops->close(&s->ctx, fidp->fs.fd);
                }
                v9fs_path_free(&path);
            }
        });
    v9fs_path_unlock(s);
    if (!err) {
        total_open_fd++;
        if (total_open_fd > open_fd_hw) {
            v9fs_reclaim_fd(pdu);
        }
    }
    return err;
}

int v9fs_co_close(V9fsPDU *pdu, int fd)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
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

int v9fs_co_fsync(V9fsPDU *pdu, V9fsFidState *fidp, int datasync)
{
    int fd, err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
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

int v9fs_co_link(V9fsPDU *pdu, V9fsFidState *oldfid,
                 V9fsFidState *newdirfid, V9fsString *name)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->link(&s->ctx, &oldfid->path,
                               &newdirfid->path, name->data);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int v9fs_co_pwritev(V9fsPDU *pdu, V9fsFidState *fidp,
                    struct iovec *iov, int iovcnt, int64_t offset)
{
    int fd, err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
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

int v9fs_co_preadv(V9fsPDU *pdu, V9fsFidState *fidp,
                   struct iovec *iov, int iovcnt, int64_t offset)
{
    int fd, err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
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
