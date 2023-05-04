/*
 * Block layer I/O functions
 *
 * Copyright (c) 2003 Fabrice Bellard
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

#ifndef BLOCK_COROUTINES_H
#define BLOCK_COROUTINES_H

#include "block/block_int.h"

/* For blk_bs() in generated block/block-gen.c */
#include "sysemu/block-backend.h"

/*
 * I/O API functions. These functions are thread-safe.
 *
 * See include/block/block-io.h for more information about
 * the I/O API.
 */

int coroutine_fn GRAPH_RDLOCK
bdrv_co_check(BlockDriverState *bs, BdrvCheckResult *res, BdrvCheckMode fix);

int coroutine_fn GRAPH_RDLOCK
bdrv_co_invalidate_cache(BlockDriverState *bs, Error **errp);

int coroutine_fn GRAPH_RDLOCK
bdrv_co_common_block_status_above(BlockDriverState *bs,
                                  BlockDriverState *base,
                                  bool include_base,
                                  bool want_zero,
                                  int64_t offset,
                                  int64_t bytes,
                                  int64_t *pnum,
                                  int64_t *map,
                                  BlockDriverState **file,
                                  int *depth);

int coroutine_fn GRAPH_RDLOCK
bdrv_co_readv_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos);

int coroutine_fn GRAPH_RDLOCK
bdrv_co_writev_vmstate(BlockDriverState *bs, QEMUIOVector *qiov, int64_t pos);

int coroutine_fn GRAPH_RDLOCK
nbd_co_do_establish_connection(BlockDriverState *bs, bool blocking,
                               Error **errp);


/*
 * "I/O or GS" API functions. These functions can run without
 * the BQL, but only in one specific iothread/main loop.
 *
 * See include/block/block-io.h for more information about
 * the "I/O or GS" API.
 */

int co_wrapper_mixed_bdrv_rdlock
bdrv_common_block_status_above(BlockDriverState *bs,
                               BlockDriverState *base,
                               bool include_base,
                               bool want_zero,
                               int64_t offset,
                               int64_t bytes,
                               int64_t *pnum,
                               int64_t *map,
                               BlockDriverState **file,
                               int *depth);

int co_wrapper_mixed_bdrv_rdlock
nbd_do_establish_connection(BlockDriverState *bs, bool blocking, Error **errp);

#endif /* BLOCK_COROUTINES_H */
