/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 * Copyright 2025 IBM Corp.
 *
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <string.h>
#include "s390-ccw.h"
#include "cio.h"
#include "virtio.h"
#include "virtio-scsi.h"
#include "virtio-ccw.h"
#include "bswap.h"
#include "helper.h"
#include "s390-time.h"

#define VRING_WAIT_REPLY_TIMEOUT 30

static VRing block[VIRTIO_MAX_VQS];
static char ring_area[VIRTIO_RING_SIZE * VIRTIO_MAX_VQS]
                     __attribute__((__aligned__(PAGE_SIZE)));

static VDev vdev = {
    .nr_vqs = 1,
    .vrings = block,
    .cmd_vr_idx = 0,
    .ring_area = ring_area,
    .wait_reply_timeout = VRING_WAIT_REPLY_TIMEOUT,
    .schid = { .one = 1 },
    .scsi_block_size = VIRTIO_SCSI_BLOCK_SIZE,
    .blk_factor = 1,
};

VDev *virtio_get_device(void)
{
    return &vdev;
}

VirtioDevType virtio_get_device_type(void)
{
    return vdev.dev_type;
}

char *virtio_get_ring_area(int ring_num)
{
    return ring_area + ring_num * VIRTIO_RING_SIZE;
}

/***********************************************
 *             Virtio functions                *
 ***********************************************/

int drain_irqs(void)
{
    switch (vdev.ipl_type) {
    case S390_IPL_TYPE_QEMU_SCSI:
    case S390_IPL_TYPE_CCW:
        return drain_irqs_ccw(vdev.schid);
    default:
        return 0;
    }
}

int virtio_run(VDev *vdev, int vqid, VirtioCmd *cmd)
{
    switch (vdev->ipl_type) {
    case S390_IPL_TYPE_QEMU_SCSI:
    case S390_IPL_TYPE_CCW:
        return virtio_ccw_run(vdev, vqid, cmd);
    default:
        return -1;
    }
}

void vring_init(VRing *vr, VqInfo *info)
{
    void *p = (void *) info->queue;

    debug_print_addr("init p", p);
    vr->id = info->index;
    vr->num = info->num;
    vr->desc = p;
    vr->avail = p + info->num * sizeof(VRingDesc);
    vr->used = (void *)(((unsigned long)&vr->avail->ring[info->num]
               + info->align - 1) & ~(info->align - 1));

    /* Zero out all relevant field */
    vr->avail->flags = 0;
    vr->avail->idx = 0;

    /* We're running with interrupts off anyways, so don't bother */
    vr->used->flags = VRING_USED_F_NO_NOTIFY;
    vr->used->idx = 0;
    vr->used_idx = 0;
    vr->next_idx = 0;
    vr->cookie = 0;

    debug_print_addr("init vr", vr);
}

bool vring_notify(VRing *vr)
{
    switch (vdev.ipl_type) {
    case S390_IPL_TYPE_QEMU_SCSI:
    case S390_IPL_TYPE_CCW:
        vr->cookie = virtio_ccw_notify(vdev.schid, vr->id, vr->cookie);
        break;
    default:
        return 1;
    }

    return vr->cookie >= 0;
}

void vring_send_buf(VRing *vr, void *p, int len, int flags)
{
    /* For follow-up chains we need to keep the first entry point */
    if (!(flags & VRING_HIDDEN_IS_CHAIN)) {
        vr->avail->ring[vr->avail->idx % vr->num] = vr->next_idx;
    }

    vr->desc[vr->next_idx].addr = (unsigned long)p;
    vr->desc[vr->next_idx].len = len;
    vr->desc[vr->next_idx].flags = flags & ~VRING_HIDDEN_IS_CHAIN;
    vr->desc[vr->next_idx].next = vr->next_idx;
    vr->desc[vr->next_idx].next++;
    vr->next_idx++;

    /* Chains only have a single ID */
    if (!(flags & VRING_DESC_F_NEXT)) {
        vr->avail->idx++;
    }
}

int vr_poll(VRing *vr)
{
    if (vr->used->idx == vr->used_idx) {
        vring_notify(vr);
        yield();
        return 0;
    }

    vr->used_idx = vr->used->idx;
    vr->next_idx = 0;
    vr->desc[0].len = 0;
    vr->desc[0].flags = 0;
    return 1; /* vr has been updated */
}

/*
 * Wait for the host to reply.
 *
 * timeout is in seconds if > 0.
 *
 * Returns 0 on success, 1 on timeout.
 */
int vring_wait_reply(void)
{
    unsigned long target_second = get_time_seconds() + vdev.wait_reply_timeout;

    /* Wait for any queue to be updated by the host */
    do {
        int i, r = 0;

        for (i = 0; i < vdev.nr_vqs; i++) {
            r += vr_poll(&vdev.vrings[i]);
        }
        yield();
        if (r) {
            return 0;
        }
    } while (!vdev.wait_reply_timeout || (get_time_seconds() < target_second));

    return 1;
}

int virtio_reset(VDev *vdev)
{
    switch (vdev->ipl_type) {
    case S390_IPL_TYPE_QEMU_SCSI:
    case S390_IPL_TYPE_CCW:
        return virtio_ccw_reset(vdev);
    default:
        return -1;
    }
}

bool virtio_is_supported(VDev *vdev)
{
    switch (vdev->ipl_type) {
    case S390_IPL_TYPE_QEMU_SCSI:
    case S390_IPL_TYPE_CCW:
        return virtio_ccw_is_supported(vdev);
    default:
        return false;
    }
}
