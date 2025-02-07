/*
 * libqos VIRTIO 1.0 PCI driver
 *
 * Copyright (c) 2019 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/pci_regs.h"
#include "standard-headers/linux/virtio_pci.h"
#include "standard-headers/linux/virtio_config.h"
#include "virtio-pci-modern.h"

static uint8_t config_readb(QVirtioDevice *d, uint64_t addr)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readb(dev->pdev, dev->bar, dev->device_cfg_offset + addr);
}

static uint16_t config_readw(QVirtioDevice *d, uint64_t addr)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readw(dev->pdev, dev->bar, dev->device_cfg_offset + addr);
}

static uint32_t config_readl(QVirtioDevice *d, uint64_t addr)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readl(dev->pdev, dev->bar, dev->device_cfg_offset + addr);
}

static uint64_t config_readq(QVirtioDevice *d, uint64_t addr)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readq(dev->pdev, dev->bar, dev->device_cfg_offset + addr);
}

static uint64_t get_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint64_t lo, hi;

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            device_feature_select),
                   0);
    lo = qpci_io_readl(dev->pdev, dev->bar, dev->common_cfg_offset +
                       offsetof(struct virtio_pci_common_cfg, device_feature));

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            device_feature_select),
                   1);
    hi = qpci_io_readl(dev->pdev, dev->bar, dev->common_cfg_offset +
                       offsetof(struct virtio_pci_common_cfg, device_feature));

    return (hi << 32) | lo;
}

static void set_features(QVirtioDevice *d, uint64_t features)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    /* Drivers must enable VIRTIO 1.0 or else use the Legacy interface */
    g_assert_cmphex(features & (1ull << VIRTIO_F_VERSION_1), !=, 0);

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature_select),
                   0);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature),
                   features);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature_select),
                   1);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature),
                   features >> 32);
}

static uint64_t get_guest_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint64_t lo, hi;

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature_select),
                   0);
    lo = qpci_io_readl(dev->pdev, dev->bar, dev->common_cfg_offset +
                       offsetof(struct virtio_pci_common_cfg, guest_feature));

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg,
                            guest_feature_select),
                   1);
    hi = qpci_io_readl(dev->pdev, dev->bar, dev->common_cfg_offset +
                       offsetof(struct virtio_pci_common_cfg, guest_feature));

    return (hi << 32) | lo;
}

static uint8_t get_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    return qpci_io_readb(dev->pdev, dev->bar, dev->common_cfg_offset +
                         offsetof(struct virtio_pci_common_cfg,
                                  device_status));
}

static void set_status(QVirtioDevice *d, uint8_t status)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    return qpci_io_writeb(dev->pdev, dev->bar, dev->common_cfg_offset +
                          offsetof(struct virtio_pci_common_cfg,
                                   device_status),
                          status);
}

static bool get_msix_status(QVirtioPCIDevice *dev, uint32_t msix_entry,
                            uint32_t msix_addr, uint32_t msix_data)
{
    uint32_t data;

    g_assert_cmpint(msix_entry, !=, -1);
    if (qpci_msix_masked(dev->pdev, msix_entry)) {
        /* No ISR checking should be done if masked, but read anyway */
        return qpci_msix_pending(dev->pdev, msix_entry);
    }

    data = qtest_readl(dev->pdev->bus->qts, msix_addr);
    if (data == msix_data) {
        qtest_writel(dev->pdev->bus->qts, msix_addr, 0);
        return true;
    } else {
        return false;
    }
}

static bool get_queue_isr_status(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    if (dev->pdev->msix_enabled) {
        QVirtQueuePCI *vqpci = container_of(vq, QVirtQueuePCI, vq);

        return get_msix_status(dev, vqpci->msix_entry, vqpci->msix_addr,
                               vqpci->msix_data);
    }

    return qpci_io_readb(dev->pdev, dev->bar, dev->isr_cfg_offset) & 1;
}

static bool get_config_isr_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    if (dev->pdev->msix_enabled) {
        return get_msix_status(dev, dev->config_msix_entry,
                               dev->config_msix_addr, dev->config_msix_data);
    }

    return qpci_io_readb(dev->pdev, dev->bar, dev->isr_cfg_offset) & 2;
}

static void wait_config_isr_status(QVirtioDevice *d, gint64 timeout_us)
{
    gint64 start_time = g_get_monotonic_time();

    while (!get_config_isr_status(d)) {
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
    }
}

static void queue_select(QVirtioDevice *d, uint16_t index)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    qpci_io_writew(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_select),
                   index);
}

static uint16_t get_queue_size(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    return qpci_io_readw(dev->pdev, dev->bar, dev->common_cfg_offset +
                         offsetof(struct virtio_pci_common_cfg, queue_size));
}

static void set_queue_address(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_desc_lo),
                   vq->desc);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_desc_hi),
                   vq->desc >> 32);

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_avail_lo),
                   vq->avail);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_avail_hi),
                   vq->avail >> 32);

    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_used_lo),
                   vq->used);
    qpci_io_writel(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_used_hi),
                   vq->used >> 32);
}

static QVirtQueue *virtqueue_setup(QVirtioDevice *d, QGuestAllocator *alloc,
                                   uint16_t index)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    QVirtQueue *vq;
    QVirtQueuePCI *vqpci;
    uint16_t notify_off;

    vq = qvirtio_pci_virtqueue_setup_common(d, alloc, index);
    vqpci = container_of(vq, QVirtQueuePCI, vq);

    notify_off = qpci_io_readw(dev->pdev, dev->bar, dev->common_cfg_offset +
                               offsetof(struct virtio_pci_common_cfg,
                                        queue_notify_off));

    vqpci->notify_offset = dev->notify_cfg_offset +
                           notify_off * dev->notify_off_multiplier;

    qpci_io_writew(dev->pdev, dev->bar, dev->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_enable), 1);

    return vq;
}

static void virtqueue_kick(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    QVirtQueuePCI *vqpci = container_of(vq, QVirtQueuePCI, vq);

    qpci_io_writew(dev->pdev, dev->bar, vqpci->notify_offset, vq->index);
}

static const QVirtioBus qvirtio_pci_virtio_1 = {
    .config_readb = config_readb,
    .config_readw = config_readw,
    .config_readl = config_readl,
    .config_readq = config_readq,
    .get_features = get_features,
    .set_features = set_features,
    .get_guest_features = get_guest_features,
    .get_status = get_status,
    .set_status = set_status,
    .get_queue_isr_status = get_queue_isr_status,
    .wait_config_isr_status = wait_config_isr_status,
    .queue_select = queue_select,
    .get_queue_size = get_queue_size,
    .set_queue_address = set_queue_address,
    .virtqueue_setup = virtqueue_setup,
    .virtqueue_cleanup = qvirtio_pci_virtqueue_cleanup_common,
    .virtqueue_kick = virtqueue_kick,
};

static void set_config_vector(QVirtioPCIDevice *d, uint16_t entry)
{
    uint16_t vector;

    qpci_io_writew(d->pdev, d->bar, d->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, msix_config), entry);
    vector = qpci_io_readw(d->pdev, d->bar, d->common_cfg_offset +
                           offsetof(struct virtio_pci_common_cfg,
                                    msix_config));
    g_assert_cmphex(vector, !=, VIRTIO_MSI_NO_VECTOR);
}

static void set_queue_vector(QVirtioPCIDevice *d, uint16_t vq_idx,
                             uint16_t entry)
{
    uint16_t vector;

    queue_select(&d->vdev, vq_idx);
    qpci_io_writew(d->pdev, d->bar, d->common_cfg_offset +
                   offsetof(struct virtio_pci_common_cfg, queue_msix_vector),
                   entry);
    vector = qpci_io_readw(d->pdev, d->bar, d->common_cfg_offset +
                           offsetof(struct virtio_pci_common_cfg,
                                    queue_msix_vector));
    g_assert_cmphex(vector, !=, VIRTIO_MSI_NO_VECTOR);
}

static const QVirtioPCIMSIXOps qvirtio_pci_msix_ops_virtio_1 = {
    .set_config_vector = set_config_vector,
    .set_queue_vector = set_queue_vector,
};

static bool probe_device_type(QVirtioPCIDevice *dev)
{
    uint16_t vendor_id;
    uint16_t device_id;

    /* "Drivers MUST match devices with the PCI Vendor ID 0x1AF4" */
    vendor_id = qpci_config_readw(dev->pdev, PCI_VENDOR_ID);
    if (vendor_id != 0x1af4) {
        return false;
    }

    /*
     * "Any PCI device with ... PCI Device ID 0x1000 through 0x107F inclusive
     * is a virtio device"
     */
    device_id = qpci_config_readw(dev->pdev, PCI_DEVICE_ID);
    if (device_id < 0x1000 || device_id > 0x107f) {
        return false;
    }

    /*
     * "Devices MAY utilize a Transitional PCI Device ID range, 0x1000 to
     * 0x103F depending on the device type"
     */
    if (device_id < 0x1040) {
        /*
         * "Transitional devices MUST have the PCI Subsystem Device ID matching
         * the Virtio Device ID"
         */
        dev->vdev.device_type = qpci_config_readw(dev->pdev, PCI_SUBSYSTEM_ID);
    } else {
        /*
         * "The PCI Device ID is calculated by adding 0x1040 to the Virtio
         * Device ID"
         */
        dev->vdev.device_type = device_id - 0x1040;
    }

    return true;
}

/* Find the first VIRTIO 1.0 PCI structure for a given type */
static bool find_structure(QVirtioPCIDevice *dev, uint8_t cfg_type,
                           uint8_t *bar, uint32_t *offset, uint32_t *length,
                           uint8_t *cfg_addr)
{
    uint8_t addr = 0;

    while ((addr = qpci_find_capability(dev->pdev, PCI_CAP_ID_VNDR,
                                        addr)) != 0) {
        uint8_t type;

        type = qpci_config_readb(dev->pdev,
                addr + offsetof(struct virtio_pci_cap, cfg_type));
        if (type != cfg_type) {
            continue;
        }

        *bar = qpci_config_readb(dev->pdev,
                addr + offsetof(struct virtio_pci_cap, bar));
        *offset = qpci_config_readl(dev->pdev,
                addr + offsetof(struct virtio_pci_cap, offset));
        *length = qpci_config_readl(dev->pdev,
                addr + offsetof(struct virtio_pci_cap, length));
        if (cfg_addr) {
            *cfg_addr = addr;
        }

        return true;
    }

    return false;
}

static bool probe_device_layout(QVirtioPCIDevice *dev)
{
    uint8_t bar;
    uint8_t cfg_addr;
    uint32_t length;

    /*
     * Due to the qpci_iomap() API we only support devices that put all
     * structures in the same PCI BAR.  Luckily this is true with QEMU.
     */

    if (!find_structure(dev, VIRTIO_PCI_CAP_COMMON_CFG, &bar,
                        &dev->common_cfg_offset, &length, NULL)) {
        return false;
    }
    dev->bar_idx = bar;

    if (!find_structure(dev, VIRTIO_PCI_CAP_NOTIFY_CFG, &bar,
                        &dev->notify_cfg_offset, &length, &cfg_addr)) {
        return false;
    }
    g_assert_cmphex(bar, ==, dev->bar_idx);

    dev->notify_off_multiplier = qpci_config_readl(dev->pdev,
            cfg_addr + offsetof(struct virtio_pci_notify_cap,
                                notify_off_multiplier));

    if (!find_structure(dev, VIRTIO_PCI_CAP_ISR_CFG, &bar,
                        &dev->isr_cfg_offset, &length, NULL)) {
        return false;
    }
    g_assert_cmphex(bar, ==, dev->bar_idx);

    if (!find_structure(dev, VIRTIO_PCI_CAP_DEVICE_CFG, &bar,
                        &dev->device_cfg_offset, &length, NULL)) {
        return false;
    }
    g_assert_cmphex(bar, ==, dev->bar_idx);

    return true;
}

/* Probe a VIRTIO 1.0 device */
bool qvirtio_pci_init_virtio_1(QVirtioPCIDevice *dev)
{
    if (!probe_device_type(dev)) {
        return false;
    }

    if (!probe_device_layout(dev)) {
        return false;
    }

    dev->vdev.bus = &qvirtio_pci_virtio_1;
    dev->msix_ops = &qvirtio_pci_msix_ops_virtio_1;
    dev->vdev.big_endian = false;
    return true;
}
