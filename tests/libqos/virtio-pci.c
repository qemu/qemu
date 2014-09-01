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

    vpcidev->config_msix_entry = -1;

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

static bool qvirtio_pci_get_queue_isr_status(QVirtioDevice *d, QVirtQueue *vq)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    QVirtQueuePCI *vqpci = (QVirtQueuePCI *)vq;
    uint32_t data;

    if (dev->pdev->msix_enabled) {
        g_assert_cmpint(vqpci->msix_entry, !=, -1);
        if (qpci_msix_masked(dev->pdev, vqpci->msix_entry)) {
            /* No ISR checking should be done if masked, but read anyway */
            return qpci_msix_pending(dev->pdev, vqpci->msix_entry);
        } else {
            data = readl(vqpci->msix_addr);
            writel(vqpci->msix_addr, 0);
            return data == vqpci->msix_data;
        }
    } else {
        return qpci_io_readb(dev->pdev, dev->addr + QVIRTIO_ISR_STATUS) & 1;
    }
}

static bool qvirtio_pci_get_config_isr_status(QVirtioDevice *d)
{
    QVirtioPCIDevice *dev = (QVirtioPCIDevice *)d;
    uint32_t data;

    if (dev->pdev->msix_enabled) {
        g_assert_cmpint(dev->config_msix_entry, !=, -1);
        if (qpci_msix_masked(dev->pdev, dev->config_msix_entry)) {
            /* No ISR checking should be done if masked, but read anyway */
            return qpci_msix_pending(dev->pdev, dev->config_msix_entry);
        } else {
            data = readl(dev->config_msix_addr);
            writel(dev->config_msix_addr, 0);
            return data == dev->config_msix_data;
        }
    } else {
        return qpci_io_readb(dev->pdev, dev->addr + QVIRTIO_ISR_STATUS) & 2;
    }
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
    QVirtQueuePCI *vqpci;

    vqpci = g_malloc0(sizeof(*vqpci));
    feat = qvirtio_pci_get_guest_features(d);

    qvirtio_pci_queue_select(d, index);
    vqpci->vq.index = index;
    vqpci->vq.size = qvirtio_pci_get_queue_size(d);
    vqpci->vq.free_head = 0;
    vqpci->vq.num_free = vqpci->vq.size;
    vqpci->vq.align = QVIRTIO_PCI_ALIGN;
    vqpci->vq.indirect = (feat & QVIRTIO_F_RING_INDIRECT_DESC) != 0;

    vqpci->msix_entry = -1;
    vqpci->msix_addr = 0;
    vqpci->msix_data = 0x12345678;

    /* Check different than 0 */
    g_assert_cmpint(vqpci->vq.size, !=, 0);

    /* Check power of 2 */
    g_assert_cmpint(vqpci->vq.size & (vqpci->vq.size - 1), ==, 0);

    addr = guest_alloc(alloc, qvring_size(vqpci->vq.size, QVIRTIO_PCI_ALIGN));
    qvring_init(alloc, &vqpci->vq, addr);
    qvirtio_pci_set_queue_address(d, vqpci->vq.desc / QVIRTIO_PCI_ALIGN);

    return &vqpci->vq;
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
    .get_queue_isr_status = qvirtio_pci_get_queue_isr_status,
    .get_config_isr_status = qvirtio_pci_get_config_isr_status,
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
    d->addr = NULL;
}

void qvirtqueue_pci_msix_setup(QVirtioPCIDevice *d, QVirtQueuePCI *vqpci,
                                        QGuestAllocator *alloc, uint16_t entry)
{
    uint16_t vector;
    uint32_t control;
    void *addr;

    g_assert(d->pdev->msix_enabled);
    addr = d->pdev->msix_table + (entry * 16);

    g_assert_cmpint(entry, >=, 0);
    g_assert_cmpint(entry, <, qpci_msix_table_size(d->pdev));
    vqpci->msix_entry = entry;

    vqpci->msix_addr = guest_alloc(alloc, 4);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_LOWER_ADDR,
                                                    vqpci->msix_addr & ~0UL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_UPPER_ADDR,
                                            (vqpci->msix_addr >> 32) & ~0UL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_DATA, vqpci->msix_data);

    control = qpci_io_readl(d->pdev, addr + PCI_MSIX_ENTRY_VECTOR_CTRL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_VECTOR_CTRL,
                                        control & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);

    qvirtio_pci_queue_select(&d->vdev, vqpci->vq.index);
    qpci_io_writew(d->pdev, d->addr + QVIRTIO_MSIX_QUEUE_VECTOR, entry);
    vector = qpci_io_readw(d->pdev, d->addr + QVIRTIO_MSIX_QUEUE_VECTOR);
    g_assert_cmphex(vector, !=, QVIRTIO_MSI_NO_VECTOR);
}

void qvirtio_pci_set_msix_configuration_vector(QVirtioPCIDevice *d,
                                        QGuestAllocator *alloc, uint16_t entry)
{
    uint16_t vector;
    uint32_t control;
    void *addr;

    g_assert(d->pdev->msix_enabled);
    addr = d->pdev->msix_table + (entry * 16);

    g_assert_cmpint(entry, >=, 0);
    g_assert_cmpint(entry, <, qpci_msix_table_size(d->pdev));
    d->config_msix_entry = entry;

    d->config_msix_data = 0x12345678;
    d->config_msix_addr = guest_alloc(alloc, 4);

    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_LOWER_ADDR,
                                                    d->config_msix_addr & ~0UL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_UPPER_ADDR,
                                            (d->config_msix_addr >> 32) & ~0UL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_DATA, d->config_msix_data);

    control = qpci_io_readl(d->pdev, addr + PCI_MSIX_ENTRY_VECTOR_CTRL);
    qpci_io_writel(d->pdev, addr + PCI_MSIX_ENTRY_VECTOR_CTRL,
                                        control & ~PCI_MSIX_ENTRY_CTRL_MASKBIT);

    qpci_io_writew(d->pdev, d->addr + QVIRTIO_MSIX_CONF_VECTOR, entry);
    vector = qpci_io_readw(d->pdev, d->addr + QVIRTIO_MSIX_CONF_VECTOR);
    g_assert_cmphex(vector, !=, QVIRTIO_MSI_NO_VECTOR);
}
