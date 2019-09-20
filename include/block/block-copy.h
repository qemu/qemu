/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_COPY_H
#define BLOCK_COPY_H

#include "block/block.h"

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef void (*ProgressResetCallbackFunc)(void *opaque);
typedef struct BlockCopyState {
    BlockBackend *source;
    BlockBackend *target;
    BdrvDirtyBitmap *copy_bitmap;
    int64_t cluster_size;
    bool use_copy_range;
    int64_t copy_range_size;
    uint64_t len;

    BdrvRequestFlags write_flags;

    /*
     * skip_unallocated:
     *
     * Used by sync=top jobs, which first scan the source node for unallocated
     * areas and clear them in the copy_bitmap.  During this process, the bitmap
     * is thus not fully initialized: It may still have bits set for areas that
     * are unallocated and should actually not be copied.
     *
     * This is indicated by skip_unallocated.
     *
     * In this case, block_copy() will query the source’s allocation status,
     * skip unallocated regions, clear them in the copy_bitmap, and invoke
     * block_copy_reset_unallocated() every time it does.
     */
    bool skip_unallocated;

    /* progress_bytes_callback: called when some copying progress is done. */
    ProgressBytesCallbackFunc progress_bytes_callback;

    /*
     * progress_reset_callback: called when some bytes reset from copy_bitmap
     * (see @skip_unallocated above). The callee is assumed to recalculate how
     * many bytes remain based on the dirty bit count of copy_bitmap.
     */
    ProgressResetCallbackFunc progress_reset_callback;
    void *progress_opaque;
} BlockCopyState;

BlockCopyState *block_copy_state_new(
        BlockDriverState *source, BlockDriverState *target,
        int64_t cluster_size, BdrvRequestFlags write_flags,
        ProgressBytesCallbackFunc progress_bytes_callback,
        ProgressResetCallbackFunc progress_reset_callback,
        void *progress_opaque, Error **errp);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t start, uint64_t bytes,
                            bool *error_is_read, bool is_write_notifier);

#endif /* BLOCK_COPY_H */
