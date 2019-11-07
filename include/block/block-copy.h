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
#include "qemu/co-shared-resource.h"

typedef struct BlockCopyInFlightReq {
    int64_t start_byte;
    int64_t end_byte;
    QLIST_ENTRY(BlockCopyInFlightReq) list;
    CoQueue wait_queue; /* coroutines blocked on this request */
} BlockCopyInFlightReq;

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef void (*ProgressResetCallbackFunc)(void *opaque);
typedef struct BlockCopyState {
    /*
     * BdrvChild objects are not owned or managed by block-copy. They are
     * provided by block-copy user and user is responsible for appropriate
     * permissions on these children.
     */
    BdrvChild *source;
    BdrvChild *target;
    BdrvDirtyBitmap *copy_bitmap;
    int64_t cluster_size;
    bool use_copy_range;
    int64_t copy_size;
    uint64_t len;
    QLIST_HEAD(, BlockCopyInFlightReq) inflight_reqs;

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

    SharedResource *mem;
} BlockCopyState;

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size,
                                     BdrvRequestFlags write_flags,
                                     Error **errp);

void block_copy_set_callbacks(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        ProgressResetCallbackFunc progress_reset_callback,
        void *progress_opaque);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t start, uint64_t bytes,
                            bool *error_is_read);

#endif /* BLOCK_COPY_H */
