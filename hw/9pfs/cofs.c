
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

int v9fs_co_readlink(V9fsState *s, V9fsPath *path, V9fsString *buf)
{
    int err;
    ssize_t len;

    buf->data = g_malloc(PATH_MAX);
    qemu_co_rwlock_rdlock(&s->rename_lock);

    v9fs_co_run_in_worker(
        {
            len = s->ops->readlink(&s->ctx, path,
                                   buf->data, PATH_MAX - 1);
            if (len > -1) {
                buf->size = len;
                buf->data[len] = 0;
                err = 0;
            } else {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    if (err) {
        g_free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
    return err;
}

int v9fs_co_statfs(V9fsState *s, V9fsPath *path, struct statfs *stbuf)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->statfs(&s->ctx, path, stbuf);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_chmod(V9fsState *s, V9fsPath *path, mode_t mode)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_mode = mode;
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->chmod(&s->ctx, path, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_utimensat(V9fsState *s, V9fsPath *path,
                      struct timespec times[2])
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->utimensat(&s->ctx, path, times);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_chown(V9fsState *s, V9fsPath *path, uid_t uid, gid_t gid)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->chown(&s->ctx, path, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_truncate(V9fsState *s, V9fsPath *path, off_t size)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->truncate(&s->ctx, path, size);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_mknod(V9fsState *s, V9fsFidState *fidp, V9fsString *name, uid_t uid,
                  gid_t gid, dev_t dev, mode_t mode, struct stat *stbuf)
{
    int err;
    V9fsPath path;
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid  = uid;
    cred.fc_gid  = gid;
    cred.fc_mode = mode;
    cred.fc_rdev = dev;
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->mknod(&s->ctx, &fidp->path, name->data, &cred);
            if (err < 0) {
                err = -errno;
            } else {
                v9fs_path_init(&path);
                err = v9fs_name_to_path(s, &fidp->path, name->data, &path);
                if (!err) {
                    err = s->ops->lstat(&s->ctx, &path, stbuf);
                    if (err < 0) {
                        err = -errno;
                    }
                }
                v9fs_path_free(&path);
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

/* Only works with path name based fid */
int v9fs_co_remove(V9fsState *s, V9fsPath *path)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->remove(&s->ctx, path->data);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_unlinkat(V9fsState *s, V9fsPath *path, V9fsString *name, int flags)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->unlinkat(&s->ctx, path, name->data, flags);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

/* Only work with path name based fid */
int v9fs_co_rename(V9fsState *s, V9fsPath *oldpath, V9fsPath *newpath)
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

int v9fs_co_renameat(V9fsState *s, V9fsPath *olddirpath, V9fsString *oldname,
                     V9fsPath *newdirpath, V9fsString *newname)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->renameat(&s->ctx, olddirpath, oldname->data,
                                   newdirpath, newname->data);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_symlink(V9fsState *s, V9fsFidState *dfidp, V9fsString *name,
                    const char *oldpath, gid_t gid, struct stat *stbuf)
{
    int err;
    FsCred cred;
    V9fsPath path;


    cred_init(&cred);
    cred.fc_uid = dfidp->uid;
    cred.fc_gid = gid;
    cred.fc_mode = 0777;
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->symlink(&s->ctx, oldpath, &dfidp->path,
                                  name->data, &cred);
            if (err < 0) {
                err = -errno;
            } else {
                v9fs_path_init(&path);
                err = v9fs_name_to_path(s, &dfidp->path, name->data, &path);
                if (!err) {
                    err = s->ops->lstat(&s->ctx, &path, stbuf);
                    if (err < 0) {
                        err = -errno;
                    }
                }
                v9fs_path_free(&path);
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

/*
 * For path name based fid we don't block. So we can
 * directly call the fs driver ops.
 */
int v9fs_co_name_to_path(V9fsState *s, V9fsPath *dirpath,
                         const char *name, V9fsPath *path)
{
    int err;
    err = s->ops->name_to_path(&s->ctx, dirpath, name, path);
    if (err < 0) {
        err = -errno;
    }
    return err;
}
