/*
 * libqos virtio PCI driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "virtio.h"
#include "virtio-pci.h"
#include "pci.h"
#include "pci-pc.h"
#include "malloc.h"
#include "malloc-pc.h"
#include "qgraph.h"
#include "standard-headers/linux/virtio_ring.h"
#include "standard-headers/linux/virtio_pci.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_regs.h"

#include "virtio-pci-modern.h"

/* virtio-pci is a superclass of all virtio-xxx-pci devices;
 * the relation between virtio-pci and virtio-xxx-pci is implicit,
 * and therefore virtio-pci does not produce virtio and is not
 * reached by any edge, not even as a "contains" edge.
 * In facts, every device is a QVirtioPCIDevice with
 * additional fields, since every one has its own
 * number of queues and various attributes.
 * Virtio-pci provides default functions to start the
 * hw and destroy the object, and nodes that want to
 * override them should always remember to call the
 * original qvirtio_pci_destructor and qvirtio_pci_start_hw.
 */

#define CONFIG_BASE(dev) (VIRTIO_PCI_CONFIG_OFF((dev)->pdev->msix_enabled))

static uint8_t qvirtio_pci_config_readb(QVirtioDevice *d, uint64_t off)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readb(dev->pdev, dev->bar, CONFIG_BASE(dev) + off);
}

/* PCI is always read in little-endian order
 * but virtio ( < 1.0) is in guest order
 * so with a big-endian guest the order has been reversed,
 * reverse it again
 * virtio-1.0 is always little-endian, like PCI
 */

static uint16_t qvirtio_pci_config_readw(QVirtioDevice *d, uint64_t off)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint16_t value;

    value = qpci_io_readw(dev->pdev, dev->bar, CONFIG_BASE(dev) + off);
    if (qvirtio_is_big_endian(d)) {
        value = bswap16(value);
    }
    return value;
}

static uint32_t qvirtio_pci_config_readl(QVirtioDevice *d, uint64_t off)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint32_t value;

    value = qpci_io_readl(dev->pdev, dev->bar, CONFIG_BASE(dev) + off);
    if (qvirtio_is_big_endian(d)) {
        value = bswap32(value);
    }
    return value;
}

static uint64_t qvirtio_pci_config_readq(QVirtioDevice *d, uint64_t off)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint64_t val;

    val = qpci_io_readq(dev->pdev, dev->bar, CONFIG_BASE(dev) + off);
    if (qvirtio_is_big_endian(d)) {
        val = bswap64(val);
    }

    return val;
}

static uint64_t qvirtio_pci_get_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readl(dev->pdev, dev->bar, VIRTIO_PCI_HOST_FEATURES);
}

static void qvirtio_pci_set_features(QVirtioDevice *d, uint64_t features)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    qpci_io_writel(dev->pdev, dev->bar, VIRTIO_PCI_GUEST_FEATURES, features);
}

static uint64_t qvirtio_pci_get_guest_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readl(dev->pdev, dev->bar, VIRTIO_PCI_GUEST_FEATURES);
}

static uint8_t qvirtio_pci_get_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readb(dev->pdev, dev->bar, VIRTIO_PCI_STATUS);
}

static void qvirtio_pci_set_status(QVirtioDevice *d, uint8_t status)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    qpci_io_writeb(dev->pdev, dev->bar, VIRTIO_PCI_STATUS, status);
}

static bool qvirtio_pci_get_queue_isr_status(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    QVirtQueuePCI *vqpci = (QVirtQueuePCI *)vq;
    uint32_t data;

    if (dev->pdev->msix_enabled) {
        g_assert_cmpint(vqpci->msix_entry, !=, -1);
        if (qpci_msix_masked(dev->pdev, vqpci->msix_entry)) {
            /* No ISR checking should be done if masked, but read anyway */
            return qpci_msix_pending(dev->pdev, vqpci->msix_entry);
        } else {
            data = qtest_readl(dev->pdev->bus->qts, vqpci->msix_addr);
            if (data == vqpci->msix_data) {
                qtest_writel(dev->pdev->bus->qts, vqpci->msix_addr, 0);
                return true;
            } else {
                return false;
            }
        }
    } else {
        return qpci_io_readb(dev->pdev, dev->bar, VIRTIO_PCI_ISR) & 1;
    }
}

static bool qvirtio_pci_get_config_isr_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint32_t data;

    if (dev->pdev->msix_enabled) {
        g_assert_cmpint(dev->config_msix_entry, !=, -1);
        if (qpci_msix_masked(dev->pdev, dev->config_msix_entry)) {
            /* No ISR checking should be done if masked, but read anyway */
            return qpci_msix_pending(dev->pdev, dev->config_msix_entry);
        } else {
            data = qtest_readl(dev->pdev->bus->qts, dev->config_msix_addr);
            if (data == dev->config_msix_data) {
                qtest_writel(dev->pdev->bus->qts, dev->config_msix_addr, 0);
                return true;
            } else {
                return false;
            }
        }
    } else {
        return qpci_io_readb(dev->pdev, dev->bar, VIRTIO_PCI_ISR) & 2;
    }
}

static void qvirtio_pci_wait_config_isr_status(QVirtioDevice *d,
                                               gint64 timeout_us)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    gint64 start_time = g_get_monotonic_time();

    do {
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
        qtest_clock_step(dev->pdev->bus->qts, 100);
    } while (!qvirtio_pci_get_config_isr_status(d));
}

static void qvirtio_pci_queue_select(QVirtioDevice *d, uint16_t index)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    qpci_io_writeb(dev->pdev, dev->bar, VIRTIO_PCI_QUEUE_SEL, index);
}

static uint16_t qvirtio_pci_get_queue_size(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    return qpci_io_readw(dev->pdev, dev->bar, VIRTIO_PCI_QUEUE_NUM);
}

static void qvirtio_pci_set_queue_address(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    uint64_t pfn = vq->desc / VIRTIO_PCI_VRING_ALIGN;

    qpci_io_writel(dev->pdev, dev->bar, VIRTIO_PCI_QUEUE_PFN, pfn);
}

QVirtQueue *qvirtio_pci_virtqueue_setup_common(QVirtioDevice *d,
                                               QGuestAllocator *alloc,
                                               uint16_t index)
{
    uint64_t feat;
    uint64_t addr;
    QVirtQueuePCI *vqpci;
    QVirtioPCIDevice *qvpcidev = container_of(d, QVirtioPCIDevice, vdev);

    vqpci = g_malloc0(sizeof(*vqpci));
    feat = d->bus->get_guest_features(d);

    d->bus->queue_select(d, index);
    vqpci->vq.vdev = d;
    vqpci->vq.index = index;
    vqpci->vq.size = d->bus->get_queue_size(d);
    vqpci->vq.free_head = 0;
    vqpci->vq.num_free = vqpci->vq.size;
    vqpci->vq.align = VIRTIO_PCI_VRING_ALIGN;
    vqpci->vq.indirect = feat & (1ull << VIRTIO_RING_F_INDIRECT_DESC);
    vqpci->vq.event = feat & (1ull << VIRTIO_RING_F_EVENT_IDX);

    vqpci->msix_entry = -1;
    vqpci->msix_addr = 0;
    vqpci->msix_data = 0x12345678;

    /* Check different than 0 */
    g_assert_cmpint(vqpci->vq.size, !=, 0);

    /* Check power of 2 */
    g_assert_cmpint(vqpci->vq.size & (vqpci->vq.size - 1), ==, 0);

    addr = guest_alloc(alloc, qvring_size(vqpci->vq.size,
                                          VIRTIO_PCI_VRING_ALIGN));
    qvring_init(qvpcidev->pdev->bus->qts, alloc, &vqpci->vq, addr);
    d->bus->set_queue_address(d, &vqpci->vq);

    return &vqpci->vq;
}

void qvirtio_pci_virtqueue_cleanup_common(QVirtQueue *vq,
                                          QGuestAllocator *alloc)
{
    QVirtQueuePCI *vqpci = container_of(vq, QVirtQueuePCI, vq);

    guest_free(alloc, vq->desc);
    g_free(vqpci);
}

static void qvirtio_pci_virtqueue_kick(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = container_of(d, QVirtioPCIDevice, vdev);
    qpci_io_writew(dev->pdev, dev->bar, VIRTIO_PCI_QUEUE_NOTIFY, vq->index);
}

static const QVirtioBus qvirtio_pci_legacy = {
    .config_readb = qvirtio_pci_config_readb,
    .config_readw = qvirtio_pci_config_readw,
    .config_readl = qvirtio_pci_config_readl,
    .config_readq = qvirtio_pci_config_readq,
    .get_features = qvirtio_pci_get_features,
    .set_features = qvirtio_pci_set_features,
    .get_guest_features = qvirtio_pci_get_guest_features,
    .get_status = qvirtio_pci_get_status,
    .set_status = qvirtio_pci_set_status,
    .get_queue_isr_status = qvirtio_pci_get_queue_isr_status,
    .wait_config_isr_status = qvirtio_pci_wait_config_isr_status,
    .queue_select = qvirtio_pci_queue_select,
    .get_queue_size = qvirtio_pci_get_queue_size,
    .set_queue_address = qvirtio_pci_set_queue_address,
    .virtqueue_setup = qvirtio_pci_virtqueue_setup_common,
    .virtqueue_cleanup = qvirtio_pci_virtqueue_cleanup_common,
    .virtqueue_kick = qvirtio_pci_virtqueue_kick,
};

static void qvirtio_pci_set_config_vector(QVirtioPCIDevice *d, uint16_t entry)
{
    uint16_t vector;

    qpci_io_writew(d->pdev, d->bar, VIRTIO_MSI_CONFIG_VECTOR, entry);
    vector = qpci_io_readw(d->pdev, d->bar, VIRTIO_MSI_CONFIG_VECTOR);
    g_assert_cmphex(vector, !=, VIRTIO_MSI_NO_VECTOR);
}

static void qvirtio_pci_set_queue_vector(QVirtioPCIDevice *d, uint16_t vq_idx,
                                         uint16_t entry)
{
    uint16_t vector;

    qvirtio_pci_queue_select(&d->vdev, vq_idx);
    qpci_io_writew(d->pdev, d->bar, VIRTIO_MSI_QUEUE_VECTOR, entry);
    vector = qpci_io_readw(d->pdev, d->bar, VIRTIO_MSI_QUEUE_VECTOR);
    g_assert_cmphex(vector, !=, VIRTIO_MSI_NO_VECTOR);
}

static const QVirtioPCIMSIXOps qvirtio_pci_msix_ops_legacy = {
    .set_config_vector = qvirtio_pci_set_config_vector,
    .set_queue_vector = qvirtio_pci_set_queue_vector,
};

void qvirtio_pci_device_enable(QVirtioPCIDevice *d)
{
    qpci_device_enable(d->pdev);
    d->bar = qpci_iomap(d->pdev, d->bar_idx, NULL);
}

void qvirtio_pci_device_disable(QVirtioPCIDevice *d)
{
    qpci_iounmap(d->pdev, d->bar);
}

void qvirtqueue_pci_msix_setup(QVirtioPCIDevice *d, QVirtQueuePCI *vqpci,
                                        QGuestAllocator *alloc, uint16_t entry)
{
    uint32_t control;
    uint64_t off;

    g_assert(d->pdev->msix_enabled);
    off = d->pdev->msix_table_off + (entry * 16);

    g_assert_cmpint(entry, >=, 0);
    g_assert_cmpint(entry, <, qpci_msix_table_size(d->pdev));
    vqpci->msix_entry = entry;

    vqpci->msix_addr = guest_alloc(alloc, 4);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_LOWER_ADDR, vqpci->msix_addr & ~0UL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_UPPER_ADDR,
                   (vqpci->msix_addr >> 32) & ~0UL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_DATA, vqpci->msix_data);

    control = qpci_io_readl(d->pdev, d->pdev->msix_table_bar,
                            off + PCI_MSIX_ENTRY_VECTOR_CTRL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_VECTOR_CTRL,
                   control & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);

    d->msix_ops->set_queue_vector(d, vqpci->vq.index, entry);
}

void qvirtio_pci_set_msix_configuration_vector(QVirtioPCIDevice *d,
                                        QGuestAllocator *alloc, uint16_t entry)
{
    uint32_t control;
    uint64_t off;

    g_assert(d->pdev->msix_enabled);
    off = d->pdev->msix_table_off + (entry * 16);

    g_assert_cmpint(entry, >=, 0);
    g_assert_cmpint(entry, <, qpci_msix_table_size(d->pdev));
    d->config_msix_entry = entry;

    d->config_msix_data = 0x12345678;
    d->config_msix_addr = guest_alloc(alloc, 4);

    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_LOWER_ADDR, d->config_msix_addr & ~0UL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_UPPER_ADDR,
                   (d->config_msix_addr >> 32) & ~0UL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_DATA, d->config_msix_data);

    control = qpci_io_readl(d->pdev, d->pdev->msix_table_bar,
                            off + PCI_MSIX_ENTRY_VECTOR_CTRL);
    qpci_io_writel(d->pdev, d->pdev->msix_table_bar,
                   off + PCI_MSIX_ENTRY_VECTOR_CTRL,
                   control & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);

    d->msix_ops->set_config_vector(d, entry);
}

void qvirtio_pci_destructor(QOSGraphObject *obj)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)obj;
    qvirtio_pci_device_disable(dev);
    g_free(dev->pdev);
}

void qvirtio_pci_start_hw(QOSGraphObject *obj)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)obj;
    qvirtio_pci_device_enable(dev);
    qvirtio_start_device(&dev->vdev);
}

static void qvirtio_pci_init_legacy(QVirtioPCIDevice *dev)
{
    dev->vdev.device_type = qpci_config_readw(dev->pdev, PCI_SUBSYSTEM_ID);
    dev->bar_idx = 0;
    dev->vdev.bus = &qvirtio_pci_legacy;
    dev->msix_ops = &qvirtio_pci_msix_ops_legacy;
    dev->vdev.big_endian = qtest_big_endian(dev->pdev->bus->qts);
}

static void qvirtio_pci_init_from_pcidev(QVirtioPCIDevice *dev, QPCIDevice *pci_dev)
{
    dev->pdev = pci_dev;
    dev->config_msix_entry = -1;

    if (!qvirtio_pci_init_virtio_1(dev)) {
        qvirtio_pci_init_legacy(dev);
    }

    /* each virtio-xxx-pci device should override at least this function */
    dev->obj.get_driver = NULL;
    dev->obj.start_hw = qvirtio_pci_start_hw;
    dev->obj.destructor = qvirtio_pci_destructor;
}

void virtio_pci_init(QVirtioPCIDevice *dev, QPCIBus *bus, QPCIAddress * addr)
{
    QPCIDevice *pci_dev = qpci_device_find(bus, addr->devfn);
    g_assert_nonnull(pci_dev);
    qvirtio_pci_init_from_pcidev(dev, pci_dev);
}

QVirtioPCIDevice *virtio_pci_new(QPCIBus *bus, QPCIAddress * addr)
{
    QVirtioPCIDevice *dev;
    QPCIDevice *pci_dev = qpci_device_find(bus, addr->devfn);
    if (!pci_dev) {
        return NULL;
    }

    dev = g_new0(QVirtioPCIDevice, 1);
    qvirtio_pci_init_from_pcidev(dev, pci_dev);
    dev->obj.free = g_free;
    return dev;
}
