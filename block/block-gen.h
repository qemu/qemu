/*
 * Block coroutine wrapping core, used by auto-generated block/block-gen.c
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2020 Virtuozzo International GmbH
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

#ifndef BLOCK_BLOCK_GEN_H
#define BLOCK_BLOCK_GEN_H

#include "block/block_int.h"

/* Base structure for argument packing structures */
typedef struct BdrvPollCo {
    AioContext *ctx;
    bool in_progress;
    Coroutine *co; /* Keep pointer here for debugging */
} BdrvPollCo;

static inline void bdrv_poll_co(BdrvPollCo *s)
{
    assert(!qemu_in_coroutine());

    aio_co_enter(s->ctx, s->co);
    AIO_WAIT_WHILE(s->ctx, s->in_progress);
}

#endif /* BLOCK_BLOCK_GEN_H */
