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

const QVirtioBus qvirtio_pci = {
    .config_readb = qvirtio_pci_config_readb,
    .config_readw = qvirtio_pci_config_readw,
    .config_readl = qvirtio_pci_config_readl,
    .config_readq = qvirtio_pci_config_readq,
    .get_status = qvirtio_pci_get_status,
    .set_status = qvirtio_pci_set_status,
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
