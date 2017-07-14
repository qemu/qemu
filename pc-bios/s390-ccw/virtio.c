/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "virtio.h"
#include "virtio-scsi.h"
#include "bswap.h"

#define VRING_WAIT_REPLY_TIMEOUT 3

static VRing block[VIRTIO_MAX_VQS];
static char ring_area[VIRTIO_RING_SIZE * VIRTIO_MAX_VQS]
                     __attribute__((__aligned__(PAGE_SIZE)));

static char chsc_page[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));

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
    register ulong r_nr asm("1") = nr;
    register ulong r_param1 asm("2") = param1;
    register ulong r_param2 asm("3") = param2;
    register ulong r_param3 asm("4") = param3;
    register long retval asm("2");

    asm volatile ("diag 2,4,0x500"
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

static int run_ccw(VDev *vdev, int cmd, void *ptr, int len)
{
    Ccw1 ccw = {};
    CmdOrb orb = {};
    Schib schib;
    int r;

    /* start command processing */
    stsch_err(vdev->schid, &schib);
    /* enable the subchannel for IPL device */
    schib.pmcw.ena = 1;
    msch(vdev->schid, &schib);

    /* start subchannel command */
    orb.fmt = 1;
    orb.cpa = (u32)(long)&ccw;
    orb.lpm = 0x80;

    ccw.cmd_code = cmd;
    ccw.cda = (long)ptr;
    ccw.count = len;

    r = ssch(vdev->schid, &orb);
    /*
     * XXX Wait until device is done processing the CCW. For now we can
     *     assume that a simple tsch will have finished the CCW processing,
     *     but the architecture allows for asynchronous operation
     */
    if (!r) {
        r = drain_irqs(vdev->schid);
    }
    return r;
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

    vr->desc[vr->next_idx].addr = (ulong)p;
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

static u64 get_clock(void)
{
    u64 r;

    asm volatile("stck %0" : "=Q" (r) : : "cc");
    return r;
}

ulong get_second(void)
{
    return (get_clock() >> 12) / 1000000;
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
    ulong target_second = get_second() + vdev.wait_reply_timeout;

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
    } while (!vdev.wait_reply_timeout || (get_second() < target_second));

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

void virtio_setup_ccw(VDev *vdev)
{
    int i, rc, cfg_size = 0;
    unsigned char status = VIRTIO_CONFIG_S_DRIVER_OK;
    struct VirtioFeatureDesc {
        uint32_t features;
        uint8_t index;
    } __attribute__((packed)) feats;

    IPL_assert(virtio_is_supported(vdev->schid), "PE");
    /* device ID has been established now */

    vdev->config.blk.blk_size = 0; /* mark "illegal" - setup started... */
    vdev->guessed_disk_nature = VIRTIO_GDN_NONE;

    run_ccw(vdev, CCW_CMD_VDEV_RESET, NULL, 0);

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
        panic("Unsupported virtio device\n");
    }
    IPL_assert(run_ccw(vdev, CCW_CMD_READ_CONF, &vdev->config, cfg_size) == 0,
               "Could not get block device configuration");

    /* Feature negotiation */
    for (i = 0; i < ARRAY_SIZE(vdev->guest_features); i++) {
        feats.features = 0;
        feats.index = i;
        rc = run_ccw(vdev, CCW_CMD_READ_FEAT, &feats, sizeof(feats));
        IPL_assert(rc == 0, "Could not get features bits");
        vdev->guest_features[i] &= bswap32(feats.features);
        feats.features = bswap32(vdev->guest_features[i]);
        rc = run_ccw(vdev, CCW_CMD_WRITE_FEAT, &feats, sizeof(feats));
        IPL_assert(rc == 0, "Could not set features bits");
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

        IPL_assert(
            run_ccw(vdev, CCW_CMD_READ_VQ_CONF, &config, sizeof(config)) == 0,
            "Could not get block device VQ configuration");
        info.num = config.num;
        vring_init(&vdev->vrings[i], &info);
        vdev->vrings[i].schid = vdev->schid;
        IPL_assert(run_ccw(vdev, CCW_CMD_SET_VQ, &info, sizeof(info)) == 0,
                   "Cannot set VQ info");
    }
    IPL_assert(
        run_ccw(vdev, CCW_CMD_WRITE_STATUS, &status, sizeof(status)) == 0,
        "Could not write status to host");
}

bool virtio_is_supported(SubChannelId schid)
{
    vdev.schid = schid;
    memset(&vdev.senseid, 0, sizeof(vdev.senseid));
    /* run sense id command */
    if (run_ccw(&vdev, CCW_CMD_SENSE_ID, &vdev.senseid, sizeof(vdev.senseid))) {
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

int enable_mss_facility(void)
{
    int ret;
    ChscAreaSda *sda_area = (ChscAreaSda *) chsc_page;

    memset(sda_area, 0, PAGE_SIZE);
    sda_area->request.length = 0x0400;
    sda_area->request.code = 0x0031;
    sda_area->operation_code = 0x2;

    ret = chsc(sda_area);
    if ((ret == 0) && (sda_area->response.code == 0x0001)) {
        return 0;
    }
    return -EIO;
}
