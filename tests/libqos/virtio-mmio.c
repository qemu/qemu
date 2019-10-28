/*
 * libqos virtio MMIO driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/virtio.h"
#include "libqos/virtio-mmio.h"
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "standard-headers/linux/virtio_ring.h"

static uint8_t qvirtio_mmio_config_readb(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return qtest_readb(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint16_t qvirtio_mmio_config_readw(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return qtest_readw(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint32_t qvirtio_mmio_config_readl(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint64_t qvirtio_mmio_config_readq(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return qtest_readq(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint64_t qvirtio_mmio_get_features(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    uint64_t lo;
    uint64_t hi = 0;

    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_HOST_FEATURES_SEL, 0);
    lo = qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_HOST_FEATURES);

    if (dev->version >= 2) {
        qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_HOST_FEATURES_SEL, 1);
        hi = qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_HOST_FEATURES);
    }

    return (hi << 32) | lo;
}

static void qvirtio_mmio_set_features(QVirtioDevice *d, uint64_t features)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    dev->features = features;
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_GUEST_FEATURES, features);

    if (dev->version >= 2) {
        qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_GUEST_FEATURES_SEL, 1);
        qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_GUEST_FEATURES,
                     features >> 32);
    }
}

static uint64_t qvirtio_mmio_get_guest_features(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return dev->features;
}

static uint8_t qvirtio_mmio_get_status(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return (uint8_t)qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_STATUS);
}

static void qvirtio_mmio_set_status(QVirtioDevice *d, uint8_t status)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_DEVICE_STATUS, (uint32_t)status);
}

static bool qvirtio_mmio_get_queue_isr_status(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    uint32_t isr;

    isr = qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_INTERRUPT_STATUS) & 1;
    if (isr != 0) {
        qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_INTERRUPT_ACK, 1);
        return true;
    }

    return false;
}

static bool qvirtio_mmio_get_config_isr_status(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    uint32_t isr;

    isr = qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_INTERRUPT_STATUS) & 2;
    if (isr != 0) {
        qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_INTERRUPT_ACK, 2);
        return true;
    }

    return false;
}

static void qvirtio_mmio_wait_config_isr_status(QVirtioDevice *d,
                                                gint64 timeout_us)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    gint64 start_time = g_get_monotonic_time();

    do {
        g_assert(g_get_monotonic_time() - start_time <= timeout_us);
        qtest_clock_step(dev->qts, 100);
    } while (!qvirtio_mmio_get_config_isr_status(d));
}

static void qvirtio_mmio_queue_select(QVirtioDevice *d, uint16_t index)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_SEL, (uint32_t)index);

    g_assert_cmphex(qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_PFN), ==, 0);
}

static uint16_t qvirtio_mmio_get_queue_size(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    return (uint16_t)qtest_readl(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_NUM_MAX);
}

static void qvirtio_mmio_set_queue_address(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    uint64_t pfn = vq->desc / dev->page_size;

    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_PFN, pfn);
}

static QVirtQueue *qvirtio_mmio_virtqueue_setup(QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t index)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    QVirtQueue *vq;
    uint64_t addr;

    vq = g_malloc0(sizeof(*vq));
    vq->vdev = d;
    qvirtio_mmio_queue_select(d, index);
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_ALIGN, dev->page_size);

    vq->index = index;
    vq->size = qvirtio_mmio_get_queue_size(d);
    vq->free_head = 0;
    vq->num_free = vq->size;
    vq->align = dev->page_size;
    vq->indirect = dev->features & (1ull << VIRTIO_RING_F_INDIRECT_DESC);
    vq->event = dev->features & (1ull << VIRTIO_RING_F_EVENT_IDX);

    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_NUM, vq->size);

    /* Check different than 0 */
    g_assert_cmpint(vq->size, !=, 0);

    /* Check power of 2 */
    g_assert_cmpint(vq->size & (vq->size - 1), ==, 0);

    addr = guest_alloc(alloc, qvring_size(vq->size, dev->page_size));
    qvring_init(dev->qts, alloc, vq, addr);
    qvirtio_mmio_set_queue_address(d, vq);

    return vq;
}

static void qvirtio_mmio_virtqueue_cleanup(QVirtQueue *vq,
                                           QGuestAllocator *alloc)
{
    guest_free(alloc, vq->desc);
    g_free(vq);
}

static void qvirtio_mmio_virtqueue_kick(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioMMIODevice *dev = container_of(d, QVirtioMMIODevice, vdev);
    qtest_writel(dev->qts, dev->addr + QVIRTIO_MMIO_QUEUE_NOTIFY, vq->index);
}

const QVirtioBus qvirtio_mmio = {
    .config_readb = qvirtio_mmio_config_readb,
    .config_readw = qvirtio_mmio_config_readw,
    .config_readl = qvirtio_mmio_config_readl,
    .config_readq = qvirtio_mmio_config_readq,
    .get_features = qvirtio_mmio_get_features,
    .set_features = qvirtio_mmio_set_features,
    .get_guest_features = qvirtio_mmio_get_guest_features,
    .get_status = qvirtio_mmio_get_status,
    .set_status = qvirtio_mmio_set_status,
    .get_queue_isr_status = qvirtio_mmio_get_queue_isr_status,
    .wait_config_isr_status = qvirtio_mmio_wait_config_isr_status,
    .queue_select = qvirtio_mmio_queue_select,
    .get_queue_size = qvirtio_mmio_get_queue_size,
    .set_queue_address = qvirtio_mmio_set_queue_address,
    .virtqueue_setup = qvirtio_mmio_virtqueue_setup,
    .virtqueue_cleanup = qvirtio_mmio_virtqueue_cleanup,
    .virtqueue_kick = qvirtio_mmio_virtqueue_kick,
};

static void *qvirtio_mmio_get_driver(void *obj, const char *interface)
{
    QVirtioMMIODevice *virtio_mmio = obj;
    if (!g_strcmp0(interface, "virtio-bus")) {
        return &virtio_mmio->vdev;
    }
    fprintf(stderr, "%s not present in virtio-mmio\n", interface);
    g_assert_not_reached();
}

static void qvirtio_mmio_start_hw(QOSGraphObject *obj)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *) obj;
    qvirtio_start_device(&dev->vdev);
}

void qvirtio_mmio_init_device(QVirtioMMIODevice *dev, QTestState *qts,
                              uint64_t addr, uint32_t page_size)
{
    uint32_t magic;
    magic = qtest_readl(qts, addr + QVIRTIO_MMIO_MAGIC_VALUE);
    g_assert(magic == ('v' | 'i' << 8 | 'r' << 16 | 't' << 24));

    dev->version = qtest_readl(qts, addr + QVIRTIO_MMIO_VERSION);
    g_assert(dev->version == 1 || dev->version == 2);

    dev->qts = qts;
    dev->addr = addr;
    dev->page_size = page_size;
    dev->vdev.device_type = qtest_readl(qts, addr + QVIRTIO_MMIO_DEVICE_ID);
    dev->vdev.bus = &qvirtio_mmio;

    qtest_writel(qts, addr + QVIRTIO_MMIO_GUEST_PAGE_SIZE, page_size);

    dev->obj.get_driver = qvirtio_mmio_get_driver;
    dev->obj.start_hw = qvirtio_mmio_start_hw;
}

static void virtio_mmio_register_nodes(void)
{
    qos_node_create_driver("virtio-mmio", NULL);
    qos_node_produces("virtio-mmio", "virtio-bus");
}

libqos_init(virtio_mmio_register_nodes);
