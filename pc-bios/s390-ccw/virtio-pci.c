/*
 * Functionality for virtio-pci
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clp.h"
#include "pci.h"
#include "helper.h"
#include "virtio.h"
#include "bswap.h"
#include "virtio-pci.h"
#include "s390-time.h"
#include <stdio.h>

/* Variable offsets used for reads/writes to modern memory regions */
VirtioPciCap c_cap; /* Common capabilities  */
VirtioPciCap d_cap; /* Device capabilities  */
VirtioPciCap n_cap; /* Notify capabilities  */
uint32_t notify_mult;
uint16_t q_notify_offset;

static int virtio_pci_set_status(uint8_t status)
{
    int rc = vpci_write_byte(c_cap.off + VPCI_C_OFFSET_STATUS, c_cap.bar, status);
    if (rc) {
        puts("Failed to write virtio-pci status");
        return -EIO;
    }

    return 0;
}

static int virtio_pci_get_status(uint8_t *status)
{
    int rc = vpci_read_byte(c_cap.off + VPCI_C_OFFSET_STATUS, c_cap.bar, status);
    if (rc) {
        puts("Failed to read virtio-pci status");
        return -EIO;
    }

    return 0;
}

/* virtio spec v1.3 section 4.1.2.1 */
void virtio_pci_id2type(VDev *vdev, uint16_t device_id)
{
    switch (device_id) {
    case 0x1042:
    case 0x1001:
        vdev->dev_type = VIRTIO_ID_BLOCK;
        break;
    default:
        vdev->dev_type = 0;
    }
}

int virtio_pci_reset(VDev *vdev)
{
    int rc;
    uint8_t status = 0;

    rc = virtio_pci_set_status(status);
    rc |= virtio_pci_get_status(&status);

    if (rc || status) {
        puts("Failed to reset virtio-pci device");
        return 1;
    }

    return 0;
}

long virtio_pci_notify(int vq_id)
{
    uint32_t offset = n_cap.off + notify_mult * q_notify_offset;
    return vpci_bswap16_write(offset, n_cap.bar, (uint16_t) vq_id);
}

/*
 * Wrappers to byte swap common data sizes then write
 */
int vpci_write_byte(uint64_t offset, uint8_t pcias, uint8_t data)
{
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, (uint64_t) data, 1);
}

int vpci_bswap16_write(uint64_t offset, uint8_t pcias, uint16_t data)
{
    uint64_t le_data = bswap16(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 2);
}

int vpci_bswap32_write(uint64_t offset, uint8_t pcias, uint32_t data)
{
    uint64_t le_data = bswap32(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 4);
}

int vpci_bswap64_write(uint64_t offset, uint8_t pcias, uint64_t data)
{
    uint64_t le_data = bswap64(data);
    return pci_write(virtio_get_device()->pci_fh, offset, pcias, le_data, 8);
}

/*
 * Wrappers to read common data sizes then byte swap
 */
int vpci_read_byte(uint64_t offset, uint8_t pcias, uint8_t *buf)
{
    return pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 1);
}

int vpci_read_bswap16(uint64_t offset, uint8_t pcias, uint16_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 2);
    *buf = bswap16(*buf);
    return rc;
}

int vpci_read_bswap32(uint64_t offset, uint8_t pcias, uint32_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 4);
    *buf = bswap32(*buf);
    return rc;
}

int vpci_read_bswap64(uint64_t offset, uint8_t pcias, uint64_t *buf)
{
    int rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, 8);
    *buf = bswap64(*buf);
    return rc;
}

/*
 * Read to an arbitrary length buffer without byte swapping
 */
int vpci_read_flex(uint64_t offset, uint8_t pcias, void *buf, int len)
{
    uint8_t readlen;
    int rc;
    int remaining = len;

    /* Read bytes in powers of 2, up to a maximum of 8 bytes per read */
    while (remaining) {
        for (int i = 3; i >= 0; i--) {
            readlen = 1 << i;
            if (remaining >= readlen) {
                break;
            }
        }

        rc = pci_read(virtio_get_device()->pci_fh, offset, pcias, buf, readlen);
        if (rc) {
            return -1;
        }

        remaining -= readlen;
        buf += readlen;
        offset += readlen;
    }

    return 0;
}

static int vpci_set_selected_vq(uint16_t queue_num)
{
    return vpci_bswap16_write(c_cap.off + VPCI_C_OFFSET_Q_SELECT, c_cap.bar, queue_num);
}

static int vpci_set_queue_enable(uint16_t enabled)
{
    return vpci_bswap16_write(c_cap.off + VPCI_C_OFFSET_Q_ENABLE, c_cap.bar, enabled);
}

static int set_pci_vq_addr(uint64_t config_off, void *addr)
{
    return vpci_bswap64_write(c_cap.off + config_off, c_cap.bar, (uint64_t) addr);
}

static int virtio_pci_get_blk_config(void)
{
    VirtioBlkConfig *cfg = &virtio_get_device()->config.blk;
    int rc = vpci_read_flex(d_cap.off, d_cap.bar, cfg, sizeof(VirtioBlkConfig));

    /* single byte fields are not touched */
    cfg->capacity = bswap64(cfg->capacity);
    cfg->size_max = bswap32(cfg->size_max);
    cfg->seg_max = bswap32(cfg->seg_max);

    cfg->geometry.cylinders = bswap16(cfg->geometry.cylinders);

    cfg->blk_size = bswap32(cfg->blk_size);
    cfg->min_io_size = bswap16(cfg->min_io_size);
    cfg->opt_io_size = bswap32(cfg->opt_io_size);

    return rc;
}

static int virtio_pci_negotiate(void)
{
    int i, rc;
    VDev *vdev = virtio_get_device();
    struct VirtioFeatureDesc {
        uint32_t features;
        uint8_t index;
    } __attribute__((packed)) feats;

    for (i = 0; i < ARRAY_SIZE(vdev->guest_features); i++) {
        feats.features = 0;
        feats.index = i;

        rc = vpci_bswap32_write(c_cap.off + VPCI_C_OFFSET_DFSELECT, c_cap.bar,
                                feats.index);
        rc |= vpci_read_flex(c_cap.off + VPCI_C_OFFSET_DF, c_cap.bar, &feats, 4);

        vdev->guest_features[i] &= bswap32(feats.features);
        feats.features = vdev->guest_features[i];


        rc |= vpci_bswap32_write(c_cap.off + VPCI_C_OFFSET_GFSELECT, c_cap.bar,
                                 feats.index);
        rc |= vpci_bswap32_write(c_cap.off + VPCI_C_OFFSET_GF, c_cap.bar,
                                 feats.features);
    }

    return rc;
}

/*
 * Find the position of the capability config within PCI configuration
 * space for a given cfg type.  Return the position if found, otherwise 0.
 */
static uint8_t virtio_pci_find_cap_pos(uint8_t cfg_type)
{
    uint8_t next, cfg;
    int rc;

    rc = vpci_read_byte(PCI_CAPABILITY_LIST, PCI_CFGBAR, &next);
    rc |= vpci_read_byte(next + 3, PCI_CFGBAR, &cfg);

    while (!rc && (cfg != cfg_type) && next) {
        rc = vpci_read_byte(next + 1, PCI_CFGBAR, &next);
        rc |= vpci_read_byte(next + 3, PCI_CFGBAR, &cfg);
    }

    return rc ? 0 : next;
}

/*
 * Read PCI configuration space to find the offset of the Common, Device, and
 * Notification memory regions within the modern memory space.
 * Returns 0 if success, 1 if a capability could not be located, or a
 * negative RC if the configuration read failed.
 */
static int virtio_pci_read_pci_cap_config(void)
{
    uint8_t pos;
    int rc;

    /* Common capabilities */
    pos = virtio_pci_find_cap_pos(VPCI_CAP_COMMON_CFG);
    if (!pos) {
        puts("Failed to locate PCI common configuration");
        return 1;
    }

    rc = vpci_read_byte(pos + VPCI_CAP_BAR, PCI_CFGBAR, &c_cap.bar);
    if (rc || vpci_read_bswap32(pos + VPCI_CAP_OFFSET, PCI_CFGBAR, &c_cap.off)) {
        puts("Failed to read PCI common configuration");
        return -EIO;
    }

    /* Device capabilities */
    pos = virtio_pci_find_cap_pos(VPCI_CAP_DEVICE_CFG);
    if (!pos) {
        puts("Failed to locate PCI device configuration");
        return 1;
    }

    rc = vpci_read_byte(pos + VPCI_CAP_BAR, PCI_CFGBAR, &d_cap.bar);
    if (rc || vpci_read_bswap32(pos + VPCI_CAP_OFFSET, PCI_CFGBAR, &d_cap.off)) {
        puts("Failed to read PCI device configuration");
        return -EIO;
    }

    /* Notification capabilities */
    pos = virtio_pci_find_cap_pos(VPCI_CAP_NOTIFY_CFG);
    if (!pos) {
        puts("Failed to locate PCI notification configuration");
        return 1;
    }

    rc = vpci_read_byte(pos + VPCI_CAP_BAR, PCI_CFGBAR, &n_cap.bar);
    if (rc || vpci_read_bswap32(pos + VPCI_CAP_OFFSET, PCI_CFGBAR, &n_cap.off)) {
        puts("Failed to read PCI notification configuration");
        return -EIO;
    }

    rc = vpci_read_bswap32(pos + VPCI_N_CAP_MULT, PCI_CFGBAR, &notify_mult);
    if (rc || vpci_read_bswap16(c_cap.off + VPCI_C_OFFSET_Q_NOFF, c_cap.bar,
                                &q_notify_offset)) {
        puts("Failed to read notification queue configuration");
        return -EIO;
    }

    return 0;
}

static int enable_pci_bus_master(void)
{
    uint16_t cmd_reg;

    if (vpci_read_bswap16(PCI_CMD_REG, PCI_CFGBAR, &cmd_reg)) {
        puts("Failed to read PCI command register");
        return -EIO;
    }

    if (vpci_bswap16_write(PCI_CMD_REG, PCI_CFGBAR, cmd_reg | PCI_BUS_MASTER_MASK)) {
        puts("Failed to enable PCI bus mastering");
        return -EIO;
    }

    return 0;
}

int virtio_pci_setup(VDev *vdev)
{
    VRing *vr;
    int rc;
    uint8_t status;
    uint16_t vq_size;
    int i = 0;

    vdev->guessed_disk_nature = VIRTIO_GDN_NONE;
    vdev->cmd_vr_idx = 0;

    if (virtio_pci_read_pci_cap_config()) {
        puts("Invalid virtio PCI capabilities");
        return -EIO;
    }

    if (enable_pci_bus_master()) {
        return -EIO;
    }

    if (virtio_reset(vdev)) {
        return -EIO;
    }

    status = VIRTIO_CONFIG_S_ACKNOWLEDGE;
    if (virtio_pci_set_status(status)) {
        puts("Virtio-pci device Failed to ACKNOWLEDGE");
        return -EIO;
    }

    vdev->guest_features[1] = VIRTIO_F_VERSION_1;
    if (virtio_pci_negotiate()) {
        panic("Virtio feature negotiation failed!");
    }

    switch (vdev->dev_type) {
    case VIRTIO_ID_BLOCK:
        vdev->nr_vqs = 1;
        vdev->cmd_vr_idx = 0;
        virtio_pci_get_blk_config();
        break;
    default:
        puts("Unsupported virtio device");
        return -ENODEV;
    }

    status |= VIRTIO_CONFIG_S_DRIVER;
    rc = virtio_pci_set_status(status);
    if (rc) {
        puts("Set status failed");
        return -EIO;
    }

    if (vpci_read_bswap16(VPCI_C_OFFSET_Q_SIZE, c_cap.bar, &vq_size)) {
        puts("Failed to read virt-queue configuration");
        return -EIO;
    }

    /* Configure virt-queues for pci */
    for (i = 0; i < vdev->nr_vqs; i++) {
        VqInfo info = {
            .queue = (unsigned long long) virtio_get_ring_area(i),
            .align = KVM_S390_VIRTIO_RING_ALIGN,
            .index = i,
            .num = vq_size,
        };

        vr = &vdev->vrings[i];
        vring_init(vr, &info);

        if (vpci_set_selected_vq(vr->id)) {
            puts("Failed to set selected virt-queue");
            return -EIO;
        }

        rc = set_pci_vq_addr(VPCI_C_OFFSET_Q_DESCLO, vr->desc);
        rc |= set_pci_vq_addr(VPCI_C_OFFSET_Q_AVAILLO, vr->avail);
        rc |= set_pci_vq_addr(VPCI_C_OFFSET_Q_USEDLO, vr->used);
        if (rc) {
            puts("Failed to configure virt-queue address");
            return -EIO;
        }

        if (vpci_set_queue_enable(true)) {
            puts("Failed to set virt-queue enabled");
            return -EIO;
        }
    }

    status |= VIRTIO_CONFIG_S_FEATURES_OK | VIRTIO_CONFIG_S_DRIVER_OK;
    return virtio_pci_set_status(status);
}

int virtio_pci_setup_device(void)
{
    VDev *vdev = virtio_get_device();

    if (enable_pci_function(&vdev->pci_fh)) {
        puts("Failed to enable PCI function");
        return -ENODEV;
    }

    return 0;
}
