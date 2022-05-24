/*
 * AioContext wait support
 *
 * Copyright (C) 2018 Red Hat, Inc.
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
#include "qemu/main-loop.h"
#include "block/aio-wait.h"

AioWait global_aio_wait;

static void dummy_bh_cb(void *opaque)
{
    /* The point is to make AIO_WAIT_WHILE()'s aio_poll() return */
}

void aio_wait_kick(void)
{
    /*
     * Paired with smp_mb in AIO_WAIT_WHILE. Here we have:
     * write(condition);
     * aio_wait_kick() {
     *      smp_mb();
     *      read(num_waiters);
     * }
     *
     * And in AIO_WAIT_WHILE:
     * write(num_waiters);
     * smp_mb();
     * read(condition);
     */
    smp_mb();

    if (qatomic_read(&global_aio_wait.num_waiters)) {
        aio_bh_schedule_oneshot(qemu_get_aio_context(), dummy_bh_cb, NULL);
    }
}

typedef struct {
    bool done;
    QEMUBHFunc *cb;
    void *opaque;
} AioWaitBHData;

/* Context: BH in IOThread */
static void aio_wait_bh(void *opaque)
{
    AioWaitBHData *data = opaque;

    data->cb(data->opaque);

    data->done = true;
    aio_wait_kick();
}

void aio_wait_bh_oneshot(AioContext *ctx, QEMUBHFunc *cb, void *opaque)
{
    AioWaitBHData data = {
        .cb = cb,
        .opaque = opaque,
    };

    assert(qemu_get_current_aio_context() == qemu_get_aio_context());

    aio_bh_schedule_oneshot(ctx, aio_wait_bh, &data);
    AIO_WAIT_WHILE(ctx, !data.done);
}
