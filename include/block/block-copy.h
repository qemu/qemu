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

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef struct BlockCopyState BlockCopyState;

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size,
                                     BdrvRequestFlags write_flags,
                                     Error **errp);

void block_copy_set_progress_callback(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        void *progress_opaque);

void block_copy_set_progress_meter(BlockCopyState *s, ProgressMeter *pm);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t offset, int64_t bytes,
                            bool *error_is_read);

BdrvDirtyBitmap *block_copy_dirty_bitmap(BlockCopyState *s);
void block_copy_set_skip_unallocated(BlockCopyState *s, bool skip);

#endif /* BLOCK_COPY_H */
