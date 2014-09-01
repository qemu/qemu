/*
 * libqos virtio PCI driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdio.h>
#include "libqtest.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"

#include "hw/pci/pci_regs.h"

typedef struct QVirtioPCIForeachData {
    void (*func)(QVirtioDevice *d, void *data);
    uint16_t device_type;
    void *user_data;
} QVirtioPCIForeachData;

static QVirtioPCIDevice *qpcidevice_to_qvirtiodevice(QPCIDevice *pdev)
{
    QVirtioPCIDevice *vpcidev;
    vpcidev = g_malloc0(sizeof(*vpcidev));

    if (pdev) {
        vpcidev->pdev = pdev;
        vpcidev->vdev.device_type =
                            qpci_config_readw(vpcidev->pdev, PCI_SUBSYSTEM_ID);
    }

    return vpcidev;
}

static void qvirtio_pci_foreach_callback(
                        QPCIDevice *dev, int devfn, void *data)
{
    QVirtioPCIForeachData *d = data;
    QVirtioPCIDevice *vpcidev = qpcidevice_to_qvirtiodevice(dev);

    if (vpcidev->vdev.device_type == d->device_type) {
        d->func(&vpcidev->vdev, d->user_data);
    } else {
        g_free(vpcidev);
    }
}

static void qvirtio_pci_assign_device(QVirtioDevice *d, void *data)
{
    QVirtioPCIDevice **vpcidev = data;
    *vpcidev = (QVirtioPCIDevice *)d;
}

static uint8_t qvirtio_pci_config_readb(QVirtioDevice *d, void *addr)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readb(dev->pdev, addr);
}

static uint16_t qvirtio_pci_config_readw(QVirtioDevice *d, void *addr)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readw(dev->pdev, addr);
}

static uint32_t qvirtio_pci_config_readl(QVirtioDevice *d, void *addr)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readl(dev->pdev, addr);
}

static uint64_t qvirtio_pci_config_readq(QVirtioDevice *d, void *addr)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    int i;
    uint64_t u64 = 0;

    if (qtest_big_endian()) {
        for (i = 0; i < 8; ++i) {
            u64 |= (uint64_t)qpci_io_readb(dev->pdev, addr + i) << (7 - i) * 8;
        }
    } else {
        for (i = 0; i < 8; ++i) {
            u64 |= (uint64_t)qpci_io_readb(dev->pdev, addr + i) << i * 8;
        }
    }

    return u64;
}

static uint32_t qvirtio_pci_get_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readl(dev->pdev, dev->addr + QVIRTIO_DEVICE_FEATURES);
}

static void qvirtio_pci_set_features(QVirtioDevice *d, uint32_t features)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    qpci_io_writel(dev->pdev, dev->addr + QVIRTIO_GUEST_FEATURES, features);
}

static uint32_t qvirtio_pci_get_guest_features(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readl(dev->pdev, dev->addr + QVIRTIO_GUEST_FEATURES);
}

static uint8_t qvirtio_pci_get_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readb(dev->pdev, dev->addr + QVIRTIO_DEVICE_STATUS);
}

static void qvirtio_pci_set_status(QVirtioDevice *d, uint8_t status)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    qpci_io_writeb(dev->pdev, dev->addr + QVIRTIO_DEVICE_STATUS, status);
}

static uint8_t qvirtio_pci_get_isr_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readb(dev->pdev, dev->addr + QVIRTIO_ISR_STATUS);
}

static void qvirtio_pci_queue_select(QVirtioDevice *d, uint16_t index)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    qpci_io_writeb(dev->pdev, dev->addr + QVIRTIO_QUEUE_SELECT, index);
}

static uint16_t qvirtio_pci_get_queue_size(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    return qpci_io_readw(dev->pdev, dev->addr + QVIRTIO_QUEUE_SIZE);
}

static void qvirtio_pci_set_queue_address(QVirtioDevice *d, uint32_t pfn)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    qpci_io_writel(dev->pdev, dev->addr + QVIRTIO_QUEUE_ADDRESS, pfn);
}

static QVirtQueue *qvirtio_pci_virtqueue_setup(QVirtioDevice *d,
                                        QGuestAllocator *alloc, uint16_t index)
{
    uint32_t feat;
    uint64_t addr;
    QVirtQueue *vq;

    vq = g_malloc0(sizeof(*vq));
    feat = qvirtio_pci_get_guest_features(d);

    qvirtio_pci_queue_select(d, index);
    vq->index = index;
    vq->size = qvirtio_pci_get_queue_size(d);
    vq->free_head = 0;
    vq->num_free = vq->size;
    vq->align = QVIRTIO_PCI_ALIGN;
    vq->indirect = (feat & QVIRTIO_F_RING_INDIRECT_DESC) != 0;

    /* Check different than 0 */
    g_assert_cmpint(vq->size, !=, 0);

    /* Check power of 2 */
    g_assert_cmpint(vq->size & (vq->size - 1), ==, 0);

    addr = guest_alloc(alloc, qvring_size(vq->size, QVIRTIO_PCI_ALIGN));
    qvring_init(alloc, vq, addr);
    qvirtio_pci_set_queue_address(d, vq->desc / QVIRTIO_PCI_ALIGN);

    /* TODO: MSI-X configuration */

    return vq;
}

static void qvirtio_pci_virtqueue_kick(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    qpci_io_writew(dev->pdev, dev->addr + QVIRTIO_QUEUE_NOTIFY, vq->index);
}

const QVirtioBus qvirtio_pci = {
    .config_readb = qvirtio_pci_config_readb,
    .config_readw = qvirtio_pci_config_readw,
    .config_readl = qvirtio_pci_config_readl,
    .config_readq = qvirtio_pci_config_readq,
    .get_features = qvirtio_pci_get_features,
    .set_features = qvirtio_pci_set_features,
    .get_guest_features = qvirtio_pci_get_guest_features,
    .get_status = qvirtio_pci_get_status,
    .set_status = qvirtio_pci_set_status,
    .get_isr_status = qvirtio_pci_get_isr_status,
    .queue_select = qvirtio_pci_queue_select,
    .get_queue_size = qvirtio_pci_get_queue_size,
    .set_queue_address = qvirtio_pci_set_queue_address,
    .virtqueue_setup = qvirtio_pci_virtqueue_setup,
    .virtqueue_kick = qvirtio_pci_virtqueue_kick,
};

void qvirtio_pci_foreach(QPCIBus *bus, uint16_t device_type,
                void (*func)(QVirtioDevice *d, void *data), void *data)
{
    QVirtioPCIForeachData d = { .func = func,
                                .device_type = device_type,
                                .user_data = data };

    qpci_device_foreach(bus, QVIRTIO_VENDOR_ID, -1,
                                qvirtio_pci_foreach_callback, &d);
}

QVirtioPCIDevice *qvirtio_pci_device_find(QPCIBus *bus, uint16_t device_type)
{
    QVirtioPCIDevice *dev = NULL;
    qvirtio_pci_foreach(bus, device_type, qvirtio_pci_assign_device, &dev);

    return dev;
}

void qvirtio_pci_device_enable(QVirtioPCIDevice *d)
{
    qpci_device_enable(d->pdev);
    d->addr = qpci_iomap(d->pdev, 0, NULL);
    g_assert(d->addr != NULL);
}

void qvirtio_pci_device_disable(QVirtioPCIDevice *d)
{
    qpci_iounmap(d->pdev, d->addr);
}
