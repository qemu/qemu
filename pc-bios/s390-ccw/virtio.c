/*
 * Virtio driver bits
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "s390-ccw.h"
#include "virtio.h"
#include "virtio-scsi.h"

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

static int drain_irqs(SubChannelId schid)
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

static bool vring_notify(VRing *vr)
{
    vr->cookie = virtio_notify(vr->schid, vr->id, vr->cookie);
    return vr->cookie >= 0;
}

static void vring_send_buf(VRing *vr, void *p, int len, int flags)
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

static int vr_poll(VRing *vr)
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
static int vring_wait_reply(void)
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

/***********************************************
 *               Virtio block                  *
 ***********************************************/

static int virtio_blk_read_many(VDev *vdev,
                                ulong sector, void *load_addr, int sec_num)
{
    VirtioBlkOuthdr out_hdr;
    u8 status;
    VRing *vr = &vdev->vrings[vdev->cmd_vr_idx];

    /* Tell the host we want to read */
    out_hdr.type = VIRTIO_BLK_T_IN;
    out_hdr.ioprio = 99;
    out_hdr.sector = virtio_sector_adjust(sector);

    vring_send_buf(vr, &out_hdr, sizeof(out_hdr), VRING_DESC_F_NEXT);

    /* This is where we want to receive data */
    vring_send_buf(vr, load_addr, virtio_get_block_size() * sec_num,
                   VRING_DESC_F_WRITE | VRING_HIDDEN_IS_CHAIN |
                   VRING_DESC_F_NEXT);

    /* status field */
    vring_send_buf(vr, &status, sizeof(u8),
                   VRING_DESC_F_WRITE | VRING_HIDDEN_IS_CHAIN);

    /* Now we can tell the host to read */
    vring_wait_reply();

    if (drain_irqs(vr->schid)) {
        /* Well, whatever status is supposed to contain... */
        status = 1;
    }
    return status;
}

int virtio_read_many(ulong sector, void *load_addr, int sec_num)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return virtio_blk_read_many(&vdev, sector, load_addr, sec_num);
    case VIRTIO_ID_SCSI:
        return virtio_scsi_read_many(&vdev, sector, load_addr, sec_num);
    }
    panic("\n! No readable IPL device !\n");
    return -1;
}

unsigned long virtio_load_direct(ulong rec_list1, ulong rec_list2,
                                 ulong subchan_id, void *load_addr)
{
    u8 status;
    int sec = rec_list1;
    int sec_num = ((rec_list2 >> 32) & 0xffff) + 1;
    int sec_len = rec_list2 >> 48;
    ulong addr = (ulong)load_addr;

    if (sec_len != virtio_get_block_size()) {
        return -1;
    }

    sclp_print(".");
    status = virtio_read_many(sec, (void *)addr, sec_num);
    if (status) {
        panic("I/O Error");
    }
    addr += sec_num * virtio_get_block_size();

    return addr;
}

int virtio_read(ulong sector, void *load_addr)
{
    return virtio_read_many(sector, load_addr, 1);
}

/*
 * Other supported value pairs, if any, would need to be added here.
 * Note: head count is always 15.
 */
static inline u8 virtio_eckd_sectors_for_block_size(int size)
{
    switch (size) {
    case 512:
        return 49;
    case 1024:
        return 33;
    case 2048:
        return 21;
    case 4096:
        return 12;
    }
    return 0;
}

VirtioGDN virtio_guessed_disk_nature(void)
{
    return vdev.guessed_disk_nature;
}

void virtio_assume_scsi(void)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        vdev.guessed_disk_nature = VIRTIO_GDN_SCSI;
        vdev.config.blk.blk_size = VIRTIO_SCSI_BLOCK_SIZE;
        vdev.config.blk.physical_block_exp = 0;
        vdev.blk_factor = 1;
        break;
    case VIRTIO_ID_SCSI:
        vdev.scsi_block_size = VIRTIO_SCSI_BLOCK_SIZE;
        break;
    }
}

void virtio_assume_iso9660(void)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        vdev.guessed_disk_nature = VIRTIO_GDN_SCSI;
        vdev.config.blk.blk_size = VIRTIO_ISO_BLOCK_SIZE;
        vdev.config.blk.physical_block_exp = 0;
        vdev.blk_factor = VIRTIO_ISO_BLOCK_SIZE / VIRTIO_SECTOR_SIZE;
        break;
    case VIRTIO_ID_SCSI:
        vdev.scsi_block_size = VIRTIO_ISO_BLOCK_SIZE;
        break;
    }
}

void virtio_assume_eckd(void)
{
    vdev.guessed_disk_nature = VIRTIO_GDN_DASD;
    vdev.blk_factor = 1;
    vdev.config.blk.physical_block_exp = 0;
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        vdev.config.blk.blk_size = 4096;
        break;
    case VIRTIO_ID_SCSI:
        vdev.config.blk.blk_size = vdev.scsi_block_size;
        break;
    }
    vdev.config.blk.geometry.heads = 15;
    vdev.config.blk.geometry.sectors =
        virtio_eckd_sectors_for_block_size(vdev.config.blk.blk_size);
}

bool virtio_disk_is_scsi(void)
{
    if (vdev.guessed_disk_nature == VIRTIO_GDN_SCSI) {
        return true;
    }
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return (vdev.config.blk.geometry.heads == 255)
            && (vdev.config.blk.geometry.sectors == 63)
            && (virtio_get_block_size()  == VIRTIO_SCSI_BLOCK_SIZE);
    case VIRTIO_ID_SCSI:
        return true;
    }
    return false;
}

bool virtio_disk_is_eckd(void)
{
    const int block_size = virtio_get_block_size();

    if (vdev.guessed_disk_nature == VIRTIO_GDN_DASD) {
        return true;
    }
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return (vdev.config.blk.geometry.heads == 15)
            && (vdev.config.blk.geometry.sectors ==
                virtio_eckd_sectors_for_block_size(block_size));
    case VIRTIO_ID_SCSI:
        return false;
    }
    return false;
}

bool virtio_ipl_disk_is_valid(void)
{
    return virtio_disk_is_scsi() || virtio_disk_is_eckd();
}

int virtio_get_block_size(void)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev.config.blk.blk_size << vdev.config.blk.physical_block_exp;
    case VIRTIO_ID_SCSI:
        return vdev.scsi_block_size;
    }
    return 0;
}

uint8_t virtio_get_heads(void)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev.config.blk.geometry.heads;
    case VIRTIO_ID_SCSI:
        return vdev.guessed_disk_nature == VIRTIO_GDN_DASD
               ? vdev.config.blk.geometry.heads : 255;
    }
    return 0;
}

uint8_t virtio_get_sectors(void)
{
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev.config.blk.geometry.sectors;
    case VIRTIO_ID_SCSI:
        return vdev.guessed_disk_nature == VIRTIO_GDN_DASD
               ? vdev.config.blk.geometry.sectors : 63;
    }
    return 0;
}

uint64_t virtio_get_blocks(void)
{
    const uint64_t factor = virtio_get_block_size() / VIRTIO_SECTOR_SIZE;
    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev.config.blk.capacity / factor;
    case VIRTIO_ID_SCSI:
        return vdev.scsi_last_block / factor;
    }
    return 0;
}

static void virtio_setup_ccw(VDev *vdev)
{
    int i, cfg_size = 0;
    unsigned char status = VIRTIO_CONFIG_S_DRIVER_OK;

    IPL_assert(virtio_is_supported(vdev->schid), "PE");
    /* device ID has been established now */

    vdev->config.blk.blk_size = 0; /* mark "illegal" - setup started... */
    vdev->guessed_disk_nature = VIRTIO_GDN_NONE;

    run_ccw(vdev, CCW_CMD_VDEV_RESET, NULL, 0);

    switch (vdev->senseid.cu_model) {
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

    /*
     * Skipping CCW_CMD_READ_FEAT. We're not doing anything fancy, and
     * we'll just stop dead anyway if anything does not work like we
     * expect it.
     */

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

void virtio_setup_device(SubChannelId schid)
{
    vdev.schid = schid;
    virtio_setup_ccw(&vdev);

    switch (vdev.senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        sclp_print("Using virtio-blk.\n");
        if (!virtio_ipl_disk_is_valid()) {
            /* make sure all getters but blocksize return 0 for
             * invalid IPL disk
             */
            memset(&vdev.config.blk, 0, sizeof(vdev.config.blk));
            virtio_assume_scsi();
        }
        break;
    case VIRTIO_ID_SCSI:
        IPL_assert(vdev.config.scsi.sense_size == VIRTIO_SCSI_SENSE_SIZE,
            "Config: sense size mismatch");
        IPL_assert(vdev.config.scsi.cdb_size == VIRTIO_SCSI_CDB_SIZE,
            "Config: CDB size mismatch");

        sclp_print("Using virtio-scsi.\n");
        virtio_scsi_setup(&vdev);
        break;
    default:
        panic("\n! No IPL device available !\n");
    }
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
