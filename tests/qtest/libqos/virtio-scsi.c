/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "standard-headers/linux/virtio_ids.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-scsi.h"

/* virtio-scsi-device */
static void *qvirtio_scsi_get_driver(QVirtioSCSI *v_scsi,
                                     const char *interface)
{
    if (!g_strcmp0(interface, "virtio-scsi")) {
        return v_scsi;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_scsi->vdev;
    }

    fprintf(stderr, "%s not present in virtio-scsi-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_scsi_device_get_driver(void *object,
                                            const char *interface)
{
    QVirtioSCSIDevice *v_scsi = object;
    return qvirtio_scsi_get_driver(&v_scsi->scsi, interface);
}

static void *virtio_scsi_device_create(void *virtio_dev,
                                          QGuestAllocator *t_alloc,
                                          void *addr)
{
    QVirtioSCSIDevice *virtio_bdevice = g_new0(QVirtioSCSIDevice, 1);
    QVirtioSCSI *interface = &virtio_bdevice->scsi;

    interface->vdev = virtio_dev;

    virtio_bdevice->obj.get_driver = qvirtio_scsi_device_get_driver;

    return &virtio_bdevice->obj;
}

/* virtio-scsi-pci */
static void *qvirtio_scsi_pci_get_driver(void *object,
                                         const char *interface)
{
    QVirtioSCSIPCI *v_scsi = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_scsi->pci_vdev.pdev;
    }
    return qvirtio_scsi_get_driver(&v_scsi->scsi, interface);
}

static void *virtio_scsi_pci_create(void *pci_bus,
                                    QGuestAllocator *t_alloc,
                                    void *addr)
{
    QVirtioSCSIPCI *virtio_spci = g_new0(QVirtioSCSIPCI, 1);
    QVirtioSCSI *interface = &virtio_spci->scsi;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;

    g_assert_cmphex(interface->vdev->device_type, ==, VIRTIO_ID_SCSI);

    obj->get_driver = qvirtio_scsi_pci_get_driver;

    return obj;
}

static void virtio_scsi_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions opts = {
        .before_cmd_line = "-drive id=drv0,if=none,file=null-co://,"
                           "file.read-zeroes=on,format=raw",
        .after_cmd_line = "-device scsi-hd,bus=vs0.0,drive=drv0",
    };

    /* virtio-scsi-device */
    opts.extra_device_opts = "id=vs0";
    qos_node_create_driver("virtio-scsi-device",
                            virtio_scsi_device_create);
    qos_node_consumes("virtio-scsi-device", "virtio-bus", &opts);
    qos_node_produces("virtio-scsi-device", "virtio-scsi");

    /* virtio-scsi-pci */
    opts.extra_device_opts = "id=vs0,addr=04.0";
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-scsi-pci", virtio_scsi_pci_create);
    qos_node_consumes("virtio-scsi-pci", "pci-bus", &opts);
    qos_node_produces("virtio-scsi-pci", "pci-device");
    qos_node_produces("virtio-scsi-pci", "virtio-scsi");
}

libqos_init(virtio_scsi_register_nodes);
