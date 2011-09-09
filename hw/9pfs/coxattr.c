
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

int v9fs_co_llistxattr(V9fsState *s, V9fsPath *path, void *value, size_t size)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->llistxattr(&s->ctx, path, value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_lgetxattr(V9fsState *s, V9fsPath *path,
                      V9fsString *xattr_name,
                      void *value, size_t size)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lgetxattr(&s->ctx, path,
                                    xattr_name->data,
                                    value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_lsetxattr(V9fsState *s, V9fsPath *path,
                      V9fsString *xattr_name, void *value,
                      size_t size, int flags)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lsetxattr(&s->ctx, path,
                                    xattr_name->data, value,
                                    size, flags);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}

int v9fs_co_lremovexattr(V9fsState *s, V9fsPath *path,
                         V9fsString *xattr_name)
{
    int err;

    qemu_co_rwlock_rdlock(&s->rename_lock);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lremovexattr(&s->ctx, path, xattr_name->data);
            if (err < 0) {
                err = -errno;
            }
        });
    qemu_co_rwlock_unlock(&s->rename_lock);
    return err;
}
