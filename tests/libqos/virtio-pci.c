/*
 * libqos virtio PCI driver
 *
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
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
