
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

int v9fs_co_readdir_r(V9fsState *s, V9fsFidState *fidp, struct dirent *dent,
                      struct dirent **result)
{
    int err;

    v9fs_co_run_in_worker(
        {
            errno = 0;
            err = s->ops->readdir_r(&s->ctx, fidp->fs.dir, dent, result);
            if (!*result && errno) {
                err = -errno;
            } else {
                err = 0;
            }
        });
    return err;
}

off_t v9fs_co_telldir(V9fsState *s, V9fsFidState *fidp)
{
    off_t err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->telldir(&s->ctx, fidp->fs.dir);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

void v9fs_co_seekdir(V9fsState *s, V9fsFidState *fidp, off_t offset)
{
    v9fs_co_run_in_worker(
        {
            s->ops->seekdir(&s->ctx, fidp->fs.dir, offset);
        });
}

void v9fs_co_rewinddir(V9fsState *s, V9fsFidState *fidp)
{
    v9fs_co_run_in_worker(
        {
            s->ops->rewinddir(&s->ctx, fidp->fs.dir);
        });
}

int v9fs_co_mkdir(V9fsState *s, V9fsFidState *fidp, V9fsString *name,
                  mode_t mode, uid_t uid, gid_t gid, struct stat *stbuf)
{
    int err;
    FsCred cred;
    V9fsPath path;

    cred_init(&cred);
    cred.fc_mode = mode;
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->mkdir(&s->ctx, &fidp->path, name->data,  &cred);
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

int v9fs_co_opendir(V9fsState *s, V9fsFidState *fidp)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            fidp->fs.dir = s->ops->opendir(&s->ctx, &fidp->path);
            if (!fidp->fs.dir) {
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

int v9fs_co_closedir(V9fsState *s, DIR *dir)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->closedir(&s->ctx, dir);
            if (err < 0) {
                err = -errno;
            }
        });
    if (!err) {
        total_open_fd--;
    }
    return err;
}
