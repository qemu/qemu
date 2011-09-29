
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

int v9fs_co_llistxattr(V9fsPDU *pdu, V9fsPath *path, void *value, size_t size)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->llistxattr(&s->ctx, path, value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int v9fs_co_lgetxattr(V9fsPDU *pdu, V9fsPath *path,
                      V9fsString *xattr_name,
                      void *value, size_t size)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lgetxattr(&s->ctx, path,
                                    xattr_name->data,
                                    value, size);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int v9fs_co_lsetxattr(V9fsPDU *pdu, V9fsPath *path,
                      V9fsString *xattr_name, void *value,
                      size_t size, int flags)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lsetxattr(&s->ctx, path,
                                    xattr_name->data, value,
                                    size, flags);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}

int v9fs_co_lremovexattr(V9fsPDU *pdu, V9fsPath *path,
                         V9fsString *xattr_name)
{
    int err;
    V9fsState *s = pdu->s;

    if (v9fs_request_cancelled(pdu)) {
        return -EINTR;
    }
    v9fs_path_read_lock(s);
    v9fs_co_run_in_worker(
        {
            err = s->ops->lremovexattr(&s->ctx, path, xattr_name->data);
            if (err < 0) {
                err = -errno;
            }
        });
    v9fs_path_unlock(s);
    return err;
}
