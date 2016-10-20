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
#include "libqos/virtio.h"
#include "libqos/virtio-mmio.h"
#include "libqos/malloc.h"
#include "libqos/malloc-generic.h"
#include "standard-headers/linux/virtio_ring.h"

static uint8_t qvirtio_mmio_config_readb(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return readb(dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint16_t qvirtio_mmio_config_readw(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return readw(dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint32_t qvirtio_mmio_config_readl(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return readl(dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint64_t qvirtio_mmio_config_readq(QVirtioDevice *d, uint64_t off)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return readq(dev->addr + QVIRTIO_MMIO_DEVICE_SPECIFIC + off);
}

static uint32_t qvirtio_mmio_get_features(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    writel(dev->addr + QVIRTIO_MMIO_HOST_FEATURES_SEL, 0);
    return readl(dev->addr + QVIRTIO_MMIO_HOST_FEATURES);
}

static void qvirtio_mmio_set_features(QVirtioDevice *d, uint32_t features)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    dev->features = features;
    writel(dev->addr + QVIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    writel(dev->addr + QVIRTIO_MMIO_GUEST_FEATURES, features);
}

static uint32_t qvirtio_mmio_get_guest_features(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return dev->features;
}

static uint8_t qvirtio_mmio_get_status(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return (uint8_t)readl(dev->addr + QVIRTIO_MMIO_DEVICE_STATUS);
}

static void qvirtio_mmio_set_status(QVirtioDevice *d, uint8_t status)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    writel(dev->addr + QVIRTIO_MMIO_DEVICE_STATUS, (uint32_t)status);
}

static bool qvirtio_mmio_get_queue_isr_status(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    uint32_t isr;

    isr = readl(dev->addr + QVIRTIO_MMIO_INTERRUPT_STATUS) & 1;
    if (isr != 0) {
        writel(dev->addr + QVIRTIO_MMIO_INTERRUPT_ACK, 1);
        return true;
    }

    return false;
}

static bool qvirtio_mmio_get_config_isr_status(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    uint32_t isr;

    isr = readl(dev->addr + QVIRTIO_MMIO_INTERRUPT_STATUS) & 2;
    if (isr != 0) {
        writel(dev->addr + QVIRTIO_MMIO_INTERRUPT_ACK, 2);
        return true;
    }

    return false;
}

static void qvirtio_mmio_queue_select(QVirtioDevice *d, uint16_t index)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    writel(dev->addr + QVIRTIO_MMIO_QUEUE_SEL, (uint32_t)index);

    g_assert_cmphex(readl(dev->addr + QVIRTIO_MMIO_QUEUE_PFN), ==, 0);
}

static uint16_t qvirtio_mmio_get_queue_size(QVirtioDevice *d)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    return (uint16_t)readl(dev->addr + QVIRTIO_MMIO_QUEUE_NUM_MAX);
}

static void qvirtio_mmio_set_queue_address(QVirtioDevice *d, uint32_t pfn)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    writel(dev->addr + QVIRTIO_MMIO_QUEUE_PFN, pfn);
}

static QVirtQueue *qvirtio_mmio_virtqueue_setup(QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t index)
{
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    QVirtQueue *vq;
    uint64_t addr;

    vq = g_malloc0(sizeof(*vq));
    qvirtio_mmio_queue_select(d, index);
    writel(dev->addr + QVIRTIO_MMIO_QUEUE_ALIGN, dev->page_size);

    vq->index = index;
    vq->size = qvirtio_mmio_get_queue_size(d);
    vq->free_head = 0;
    vq->num_free = vq->size;
    vq->align = dev->page_size;
    vq->indirect = (dev->features & (1u << VIRTIO_RING_F_INDIRECT_DESC)) != 0;
    vq->event = (dev->features & (1u << VIRTIO_RING_F_EVENT_IDX)) != 0;

    writel(dev->addr + QVIRTIO_MMIO_QUEUE_NUM, vq->size);

    /* Check different than 0 */
    g_assert_cmpint(vq->size, !=, 0);

    /* Check power of 2 */
    g_assert_cmpint(vq->size & (vq->size - 1), ==, 0);

    addr = guest_alloc(alloc, qvring_size(vq->size, dev->page_size));
    qvring_init(alloc, vq, addr);
    qvirtio_mmio_set_queue_address(d, vq->desc / dev->page_size);

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
    QVirtioMMIODevice *dev = (QVirtioMMIODevice *)d;
    writel(dev->addr + QVIRTIO_MMIO_QUEUE_NOTIFY, vq->index);
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
    .get_config_isr_status = qvirtio_mmio_get_config_isr_status,
    .queue_select = qvirtio_mmio_queue_select,
    .get_queue_size = qvirtio_mmio_get_queue_size,
    .set_queue_address = qvirtio_mmio_set_queue_address,
    .virtqueue_setup = qvirtio_mmio_virtqueue_setup,
    .virtqueue_cleanup = qvirtio_mmio_virtqueue_cleanup,
    .virtqueue_kick = qvirtio_mmio_virtqueue_kick,
};

QVirtioMMIODevice *qvirtio_mmio_init_device(uint64_t addr, uint32_t page_size)
{
    QVirtioMMIODevice *dev;
    uint32_t magic;
    dev = g_malloc0(sizeof(*dev));

    magic = readl(addr + QVIRTIO_MMIO_MAGIC_VALUE);
    g_assert(magic == ('v' | 'i' << 8 | 'r' << 16 | 't' << 24));

    dev->addr = addr;
    dev->page_size = page_size;
    dev->vdev.device_type = readl(addr + QVIRTIO_MMIO_DEVICE_ID);
    dev->vdev.bus = &qvirtio_mmio;

    writel(addr + QVIRTIO_MMIO_GUEST_PAGE_SIZE, page_size);

    return dev;
}
