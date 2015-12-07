/*
 * Virtio Block Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VIRTIO_BLK_H
#define _QEMU_VIRTIO_BLK_H

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/virtio.h"
#include "hw/block/block.h"
#include "sysemu/iothread.h"
#include "sysemu/block-backend.h"

#define TYPE_VIRTIO_BLK "virtio-blk-device"
#define VIRTIO_BLK(obj) \
        OBJECT_CHECK(VirtIOBlock, (obj), TYPE_VIRTIO_BLK)

/* This is the last element of the write scatter-gather list */
struct virtio_blk_inhdr
{
    unsigned char status;
};

struct VirtIOBlkConf
{
    BlockConf conf;
    IOThread *iothread;
    char *serial;
    uint32_t scsi;
    uint32_t config_wce;
    uint32_t request_merging;
};

struct VirtIOBlockDataPlane;

struct VirtIOBlockReq;
typedef struct VirtIOBlock {
    VirtIODevice parent_obj;
    BlockBackend *blk;
    VirtQueue *vq;
    void *rq;
    QEMUBH *bh;
    VirtIOBlkConf conf;
    unsigned short sector_mask;
    bool original_wce;
    VMChangeStateEntry *change;
    /* Function to push to vq and notify guest */
    void (*complete_request)(struct VirtIOBlockReq *req, unsigned char status);
    Notifier migration_state_notifier;
    struct VirtIOBlockDataPlane *dataplane;
} VirtIOBlock;

typedef struct VirtIOBlockReq {
    int64_t sector_num;
    VirtIOBlock *dev;
    VirtQueueElement elem;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    QEMUIOVector qiov;
    size_t in_len;
    struct VirtIOBlockReq *next;
    struct VirtIOBlockReq *mr_next;
    BlockAcctCookie acct;
} VirtIOBlockReq;

#define VIRTIO_BLK_MAX_MERGE_REQS 32

typedef struct MultiReqBuffer {
    VirtIOBlockReq *reqs[VIRTIO_BLK_MAX_MERGE_REQS];
    unsigned int num_reqs;
    bool is_write;
} MultiReqBuffer;

VirtIOBlockReq *virtio_blk_alloc_request(VirtIOBlock *s);

void virtio_blk_free_request(VirtIOBlockReq *req);

void virtio_blk_handle_request(VirtIOBlockReq *req, MultiReqBuffer *mrb);

void virtio_blk_submit_multireq(BlockBackend *blk, MultiReqBuffer *mrb);

#endif
