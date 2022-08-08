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

#define VIRTIO_BLK_F_GEOMETRY   (1 << 4)
#define VIRTIO_BLK_F_BLK_SIZE   (1 << 6)

static int virtio_blk_read_many(VDev *vdev, ulong sector, void *load_addr,
                                int sec_num)
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
    VDev *vdev = virtio_get_device();

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return virtio_blk_read_many(vdev, sector, load_addr, sec_num);
    case VIRTIO_ID_SCSI:
        return virtio_scsi_read_many(vdev, sector, load_addr, sec_num);
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
    return virtio_get_device()->guessed_disk_nature;
}

void virtio_assume_iso9660(void)
{
    VDev *vdev = virtio_get_device();

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        vdev->guessed_disk_nature = VIRTIO_GDN_SCSI;
        vdev->config.blk.blk_size = VIRTIO_ISO_BLOCK_SIZE;
        vdev->config.blk.physical_block_exp = 0;
        vdev->blk_factor = VIRTIO_ISO_BLOCK_SIZE / VIRTIO_SECTOR_SIZE;
        break;
    case VIRTIO_ID_SCSI:
        vdev->scsi_block_size = VIRTIO_ISO_BLOCK_SIZE;
        break;
    }
}

void virtio_assume_eckd(void)
{
    VDev *vdev = virtio_get_device();

    vdev->guessed_disk_nature = VIRTIO_GDN_DASD;
    vdev->blk_factor = 1;
    vdev->config.blk.physical_block_exp = 0;
    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        vdev->config.blk.blk_size = VIRTIO_DASD_DEFAULT_BLOCK_SIZE;
        break;
    case VIRTIO_ID_SCSI:
        vdev->config.blk.blk_size = vdev->scsi_block_size;
        break;
    }
    vdev->config.blk.geometry.heads = 15;
    vdev->config.blk.geometry.sectors =
        virtio_eckd_sectors_for_block_size(vdev->config.blk.blk_size);
}

bool virtio_ipl_disk_is_valid(void)
{
    int blksize = virtio_get_block_size();
    VDev *vdev = virtio_get_device();

    if (vdev->guessed_disk_nature == VIRTIO_GDN_SCSI ||
        vdev->guessed_disk_nature == VIRTIO_GDN_DASD) {
        return true;
    }

    return (vdev->senseid.cu_model == VIRTIO_ID_BLOCK ||
            vdev->senseid.cu_model == VIRTIO_ID_SCSI) &&
           blksize >= 512 && blksize <= 4096;
}

int virtio_get_block_size(void)
{
    VDev *vdev = virtio_get_device();

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev->config.blk.blk_size;
    case VIRTIO_ID_SCSI:
        return vdev->scsi_block_size;
    }
    return 0;
}

uint8_t virtio_get_heads(void)
{
    VDev *vdev = virtio_get_device();

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev->config.blk.geometry.heads;
    case VIRTIO_ID_SCSI:
        return vdev->guessed_disk_nature == VIRTIO_GDN_DASD
               ? vdev->config.blk.geometry.heads : 255;
    }
    return 0;
}

uint8_t virtio_get_sectors(void)
{
    VDev *vdev = virtio_get_device();

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev->config.blk.geometry.sectors;
    case VIRTIO_ID_SCSI:
        return vdev->guessed_disk_nature == VIRTIO_GDN_DASD
               ? vdev->config.blk.geometry.sectors : 63;
    }
    return 0;
}

uint64_t virtio_get_blocks(void)
{
    VDev *vdev = virtio_get_device();
    const uint64_t factor = virtio_get_block_size() / VIRTIO_SECTOR_SIZE;

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_BLOCK:
        return vdev->config.blk.capacity / factor;
    case VIRTIO_ID_SCSI:
        return vdev->scsi_last_block / factor;
    }
    return 0;
}

int virtio_blk_setup_device(SubChannelId schid)
{
    VDev *vdev = virtio_get_device();

    vdev->guest_features[0] = VIRTIO_BLK_F_GEOMETRY | VIRTIO_BLK_F_BLK_SIZE;
    vdev->schid = schid;
    virtio_setup_ccw(vdev);

    sclp_print("Using virtio-blk.\n");

    return 0;
}
