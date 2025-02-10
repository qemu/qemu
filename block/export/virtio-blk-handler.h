/*
 * Handler for virtio-blk I/O
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VIRTIO_BLK_HANDLER_H
#define VIRTIO_BLK_HANDLER_H

#include "system/block-backend.h"

#define VIRTIO_BLK_SECTOR_BITS 9
#define VIRTIO_BLK_SECTOR_SIZE (1ULL << VIRTIO_BLK_SECTOR_BITS)

#define VIRTIO_BLK_MAX_DISCARD_SECTORS 32768
#define VIRTIO_BLK_MAX_WRITE_ZEROES_SECTORS 32768

typedef struct {
    BlockBackend *blk;
    char *serial;
    uint32_t logical_block_size;
    bool writable;
} VirtioBlkHandler;

int coroutine_fn virtio_blk_process_req(VirtioBlkHandler *handler,
                                        struct iovec *in_iov,
                                        struct iovec *out_iov,
                                        unsigned int in_num,
                                        unsigned int out_num);

#endif /* VIRTIO_BLK_HANDLER_H */
