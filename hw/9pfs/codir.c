
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

int v9fs_co_readdir(V9fsState *s, V9fsFidState *fidp, struct dirent **dent)
{
    int err;

    v9fs_co_run_in_worker(
        {
            errno = 0;
            /*FIXME!! need to switch to readdir_r */
            *dent = s->ops->readdir(&s->ctx, fidp->fs.dir);
            if (!*dent && errno) {
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

int v9fs_co_mkdir(V9fsState *s, char *name, mode_t mode, uid_t uid, gid_t gid)
{
    int err;
    FsCred cred;

    cred_init(&cred);
    cred.fc_mode = mode;
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    v9fs_co_run_in_worker(
        {
            err = s->ops->mkdir(&s->ctx, name, &cred);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}
