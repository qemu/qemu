
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

    buf->data = qemu_malloc(PATH_MAX);
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
        qemu_free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
    return err;
}
