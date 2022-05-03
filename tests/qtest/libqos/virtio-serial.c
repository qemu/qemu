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
#include "../libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "virtio-serial.h"

static void *qvirtio_serial_get_driver(QVirtioSerial *v_serial,
                                       const char *interface)
{
    if (!g_strcmp0(interface, "virtio-serial")) {
        return v_serial;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_serial->vdev;
    }

    fprintf(stderr, "%s not present in virtio-serial-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_serial_device_get_driver(void *object,
                                              const char *interface)
{
    QVirtioSerialDevice *v_serial = object;
    return qvirtio_serial_get_driver(&v_serial->serial, interface);
}

static void *virtio_serial_device_create(void *virtio_dev,
                                         QGuestAllocator *t_alloc,
                                         void *addr)
{
    QVirtioSerialDevice *virtio_device = g_new0(QVirtioSerialDevice, 1);
    QVirtioSerial *interface = &virtio_device->serial;

    interface->vdev = virtio_dev;

    virtio_device->obj.get_driver = qvirtio_serial_device_get_driver;

    return &virtio_device->obj;
}

/* virtio-serial-pci */
static void *qvirtio_serial_pci_get_driver(void *object, const char *interface)
{
    QVirtioSerialPCI *v_serial = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_serial->pci_vdev.pdev;
    }
    return qvirtio_serial_get_driver(&v_serial->serial, interface);
}

static void *virtio_serial_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVirtioSerialPCI *virtio_spci = g_new0(QVirtioSerialPCI, 1);
    QVirtioSerial *interface = &virtio_spci->serial;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;

    obj->get_driver = qvirtio_serial_pci_get_driver;

    return obj;
}

static void virtio_serial_register_nodes(void)
{
   QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions edge_opts = { };

    /* virtio-serial-device */
    edge_opts.extra_device_opts = "id=vser0";
    qos_node_create_driver("virtio-serial-device",
                            virtio_serial_device_create);
    qos_node_consumes("virtio-serial-device", "virtio-bus", &edge_opts);
    qos_node_produces("virtio-serial-device", "virtio");
    qos_node_produces("virtio-serial-device", "virtio-serial");

    /* virtio-serial-pci */
    edge_opts.extra_device_opts = "id=vser0,addr=04.0";
    add_qpci_address(&edge_opts, &addr);
    qos_node_create_driver("virtio-serial-pci", virtio_serial_pci_create);
    qos_node_consumes("virtio-serial-pci", "pci-bus", &edge_opts);
    qos_node_produces("virtio-serial-pci", "pci-device");
    qos_node_produces("virtio-serial-pci", "virtio");
    qos_node_produces("virtio-serial-pci", "virtio-serial");
}

libqos_init(virtio_serial_register_nodes);
