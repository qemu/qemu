/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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
    return vdev.senseid.cu_model;
}

/* virtio spec v1.0 para 4.3.3.2 */
static long kvm_hypercall(unsigned long nr, unsigned long param1,
                          unsigned long param2, unsigned long param3)
{
    register unsigned long r_nr asm("1") = nr;
    register unsigned long r_param1 asm("2") = param1;
    register unsigned long r_param2 asm("3") = param2;
    register unsigned long r_param3 asm("4") = param3;
    register long retval asm("2");

    asm volatile ("diag %%r2,%%r4,0x500"
                  : "=d" (retval)
                  : "d" (r_nr), "0" (r_param1), "r"(r_param2), "d"(r_param3)
                  : "memory", "cc");

    return retval;
}

static long virtio_notify(SubChannelId schid, int vq_idx, long cookie)
{
    return kvm_hypercall(KVM_S390_VIRTIO_CCW_NOTIFY, *(u32 *)&schid,
                         vq_idx, cookie);
}

/***********************************************
 *             Virtio functions                *
 ***********************************************/

int drain_irqs(SubChannelId schid)
{
    Irb irb = {};
    int r = 0;

    while (1) {
        /* FIXME: make use of TPI, for that enable subchannel and isc */
        if (tsch(schid, &irb)) {
            /* Might want to differentiate error codes later on. */
            if (irb.scsw.cstat) {
                r = -EIO;
            } else if (irb.scsw.dstat != 0xc) {
                r = -EIO;
            }
            return r;
        }
    }
}

static int run_ccw(VDev *vdev, int cmd, void *ptr, int len, bool sli)
{
    Ccw1 ccw = {};

    ccw.cmd_code = cmd;
    ccw.cda = (long)ptr;
    ccw.count = len;

    if (sli) {
        ccw.flags |= CCW_FLAG_SLI;
    }

    return do_cio(vdev->schid, vdev->senseid.cu_type, ptr2u32(&ccw), CCW_FMT1);
}

static void vring_init(VRing *vr, VqInfo *info)
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
    vr->cookie = virtio_notify(vr->schid, vr->id, vr->cookie);
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

int virtio_run(VDev *vdev, int vqid, VirtioCmd *cmd)
{
    VRing *vr = &vdev->vrings[vqid];
    int i = 0;

    do {
        vring_send_buf(vr, cmd[i].data, cmd[i].size,
                       cmd[i].flags | (i ? VRING_HIDDEN_IS_CHAIN : 0));
    } while (cmd[i++].flags & VRING_DESC_F_NEXT);

    vring_wait_reply();
    if (drain_irqs(vr->schid)) {
        return -1;
    }
    return 0;
}

int virtio_reset(VDev *vdev)
{
    return run_ccw(vdev, CCW_CMD_VDEV_RESET, NULL, 0, false);
}

int virtio_setup_ccw(VDev *vdev)
{
    int i, cfg_size = 0;
    uint8_t status;
    struct VirtioFeatureDesc {
        uint32_t features;
        uint8_t index;
    } __attribute__((packed)) feats;

    if (!virtio_is_supported(vdev->schid)) {
        puts("Virtio unsupported for this device ID");
        return -ENODEV;
    }
    /* device ID has been established now */

    vdev->config.blk.blk_size = 0; /* mark "illegal" - setup started... */
    vdev->guessed_disk_nature = VIRTIO_GDN_NONE;

    virtio_reset(vdev);

    status = VIRTIO_CONFIG_S_ACKNOWLEDGE;
    if (run_ccw(vdev, CCW_CMD_WRITE_STATUS, &status, sizeof(status), false)) {
        puts("Could not write ACKNOWLEDGE status to host");
        return -EIO;
    }

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_NET:
        vdev->nr_vqs = 2;
        vdev->cmd_vr_idx = 0;
        cfg_size = sizeof(vdev->config.net);
        break;
    case VIRTIO_ID_BLOCK:
        vdev->nr_vqs = 1;
        vdev->cmd_vr_idx = 0;
        cfg_size = sizeof(vdev->config.blk);
        break;
    case VIRTIO_ID_SCSI:
        vdev->nr_vqs = 3;
        vdev->cmd_vr_idx = VR_REQUEST;
        cfg_size = sizeof(vdev->config.scsi);
        break;
    default:
        puts("Unsupported virtio device");
        return -ENODEV;
    }

    status |= VIRTIO_CONFIG_S_DRIVER;
    if (run_ccw(vdev, CCW_CMD_WRITE_STATUS, &status, sizeof(status), false)) {
        puts("Could not write DRIVER status to host");
        return -EIO;
    }

    /* Feature negotiation */
    for (i = 0; i < ARRAY_SIZE(vdev->guest_features); i++) {
        feats.features = 0;
        feats.index = i;
        if (run_ccw(vdev, CCW_CMD_READ_FEAT, &feats, sizeof(feats), false)) {
            puts("Could not get features bits");
            return -EIO;
        }

        vdev->guest_features[i] &= bswap32(feats.features);
        feats.features = bswap32(vdev->guest_features[i]);
        if (run_ccw(vdev, CCW_CMD_WRITE_FEAT, &feats, sizeof(feats), false)) {
            puts("Could not set features bits");
            return -EIO;
        }
    }

    if (run_ccw(vdev, CCW_CMD_READ_CONF, &vdev->config, cfg_size, false)) {
        puts("Could not get virtio device configuration");
        return -EIO;
    }

    for (i = 0; i < vdev->nr_vqs; i++) {
        VqInfo info = {
            .queue = (unsigned long long) ring_area + (i * VIRTIO_RING_SIZE),
            .align = KVM_S390_VIRTIO_RING_ALIGN,
            .index = i,
            .num = 0,
        };
        VqConfig config = {
            .index = i,
            .num = 0,
        };

        if (run_ccw(vdev, CCW_CMD_READ_VQ_CONF, &config, sizeof(config),
                false)) {
            puts("Could not get virtio device VQ config");
            return -EIO;
        }
        info.num = config.num;
        vring_init(&vdev->vrings[i], &info);
        vdev->vrings[i].schid = vdev->schid;
        if (run_ccw(vdev, CCW_CMD_SET_VQ, &info, sizeof(info), false)) {
            puts("Cannot set VQ info");
            return -EIO;
        }
    }

    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    if (run_ccw(vdev, CCW_CMD_WRITE_STATUS, &status, sizeof(status), false)) {
        puts("Could not write DRIVER_OK status to host");
        return -EIO;
    }

    return 0;
}

bool virtio_is_supported(SubChannelId schid)
{
    vdev.schid = schid;
    memset(&vdev.senseid, 0, sizeof(vdev.senseid));

    /*
     * Run sense id command.
     * The size of the senseid data differs between devices (notably,
     * between virtio devices and dasds), so specify the largest possible
     * size and suppress the incorrect length indication for smaller sizes.
     */
    if (run_ccw(&vdev, CCW_CMD_SENSE_ID, &vdev.senseid, sizeof(vdev.senseid),
                true)) {
        return false;
    }
    if (vdev.senseid.cu_type == 0x3832) {
        switch (vdev.senseid.cu_model) {
        case VIRTIO_ID_BLOCK:
        case VIRTIO_ID_SCSI:
        case VIRTIO_ID_NET:
            return true;
        }
    }
    return false;
}
