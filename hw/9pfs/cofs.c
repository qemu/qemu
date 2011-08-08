
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

int v9fs_co_readlink(V9fsState *s, V9fsString *path, V9fsString *buf)
{
    int err;
    ssize_t len;

    buf->data = g_malloc(PATH_MAX);
    v9fs_co_run_in_worker(
        {
            len = s->ops->readlink(&s->ctx, path->data,
                                   buf->data, PATH_MAX - 1);
            if (len > -1) {
                buf->size = len;
                buf->data[len] = 0;
                err = 0;
            } else {
                err = -errno;
            }
        });
    if (err) {
        g_free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
    return err;
}

int v9fs_co_statfs(V9fsState *s, V9fsString *path, struct statfs *stbuf)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->statfs(&s->ctx, path->data, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_chmod(V9fsState *s, V9fsString *path, mode_t mode)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_mode = mode;
    v9fs_co_run_in_worker(
        {
            err = s->ops->chmod(&s->ctx, path->data, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_utimensat(V9fsState *s, V9fsString *path,
                      struct timespec times[2])
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->utimensat(&s->ctx, path->data, times);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_chown(V9fsState *s, V9fsString *path, uid_t uid, gid_t gid)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    v9fs_co_run_in_worker(
        {
            err = s->ops->chown(&s->ctx, path->data, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_truncate(V9fsState *s, V9fsString *path, off_t size)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->truncate(&s->ctx, path->data, size);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_mknod(V9fsState *s, V9fsString *path, uid_t uid,
                  gid_t gid, dev_t dev, mode_t mode)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid  = uid;
    cred.fc_gid  = gid;
    cred.fc_mode = mode;
    cred.fc_rdev = dev;
    v9fs_co_run_in_worker(
        {
            err = s->ops->mknod(&s->ctx, path->data, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_remove(V9fsState *s, V9fsString *path)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->remove(&s->ctx, path->data);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_rename(V9fsState *s, V9fsString *oldpath, V9fsString *newpath)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->rename(&s->ctx, oldpath->data, newpath->data);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_symlink(V9fsState *s, V9fsFidState *fidp,
                    const char *oldpath, const char *newpath, gid_t gid)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid = fidp->uid;
    cred.fc_gid = gid;
    cred.fc_mode = 0777;
    v9fs_co_run_in_worker(
        {
            err = s->ops->symlink(&s->ctx, oldpath, newpath, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}
