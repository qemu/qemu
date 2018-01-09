/*
 * Block layer code related to image creation
 *
 * Copyright (c) 2018 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/error.h"

typedef struct BlockdevCreateCo {
    BlockDriver *drv;
    BlockdevCreateOptions *opts;
    int ret;
    Error **errp;
} BlockdevCreateCo;

static void coroutine_fn bdrv_co_create_co_entry(void *opaque)
{
    BlockdevCreateCo *cco = opaque;
    cco->ret = cco->drv->bdrv_co_create(cco->opts, cco->errp);
}

void qmp_x_blockdev_create(BlockdevCreateOptions *options, Error **errp)
{
    const char *fmt = BlockdevDriver_str(options->driver);
    BlockDriver *drv = bdrv_find_format(fmt);
    Coroutine *co;
    BlockdevCreateCo cco;

    /* If the driver is in the schema, we know that it exists. But it may not
     * be whitelisted. */
    assert(drv);
    if (bdrv_uses_whitelist() && !bdrv_is_whitelisted(drv, false)) {
        error_setg(errp, "Driver is not whitelisted");
        return;
    }

    /* Call callback if it exists */
    if (!drv->bdrv_co_create) {
        error_setg(errp, "Driver does not support blockdev-create");
        return;
    }

    cco = (BlockdevCreateCo) {
        .drv = drv,
        .opts = options,
        .ret = -EINPROGRESS,
        .errp = errp,
    };

    co = qemu_coroutine_create(bdrv_co_create_co_entry, &cco);
    qemu_coroutine_enter(co);
    while (cco.ret == -EINPROGRESS) {
        aio_poll(qemu_get_aio_context(), true);
    }
}
