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

#ifndef BLOCK_COROUTINES_INT_H
#define BLOCK_COROUTINES_INT_H

#include "block/block_int.h"

int coroutine_fn bdrv_co_check(BlockDriverState *bs,
                               BdrvCheckResult *res, BdrvCheckMode fix);
int coroutine_fn bdrv_co_invalidate_cache(BlockDriverState *bs, Error **errp);

int generated_co_wrapper
bdrv_preadv(BdrvChild *child, int64_t offset, unsigned int bytes,
            QEMUIOVector *qiov, BdrvRequestFlags flags);
int generated_co_wrapper
bdrv_pwritev(BdrvChild *child, int64_t offset, unsigned int bytes,
             QEMUIOVector *qiov, BdrvRequestFlags flags);

int coroutine_fn
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
int generated_co_wrapper
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

int coroutine_fn bdrv_co_readv_vmstate(BlockDriverState *bs,
                                       QEMUIOVector *qiov, int64_t pos);
int coroutine_fn bdrv_co_writev_vmstate(BlockDriverState *bs,
                                        QEMUIOVector *qiov, int64_t pos);

#endif /* BLOCK_COROUTINES_INT_H */
