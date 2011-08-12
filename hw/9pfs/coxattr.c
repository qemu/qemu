
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

int v9fs_co_llistxattr(V9fsState *s, V9fsString *path, void *value, size_t size)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->llistxattr(&s->ctx, path->data, value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}

int v9fs_co_lgetxattr(V9fsState *s, V9fsString *path,
                      V9fsString *xattr_name,
                      void *value, size_t size)
{
    int err;

    v9fs_co_run_in_worker(
        {
            err = s->ops->lgetxattr(&s->ctx, path->data,
                                    xattr_name->data,
                                    value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    return err;
}
