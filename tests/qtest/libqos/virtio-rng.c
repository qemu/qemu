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
#include "virtio-rng.h"

/* virtio-rng-device */
static void *qvirtio_rng_get_driver(QVirtioRng *v_rng,
                                    const char *interface)
{
    if (!g_strcmp0(interface, "virtio-rng")) {
        return v_rng;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_rng->vdev;
    }

    fprintf(stderr, "%s not present in virtio-rng-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_rng_device_get_driver(void *object,
                                           const char *interface)
{
    QVirtioRngDevice *v_rng = object;
    return qvirtio_rng_get_driver(&v_rng->rng, interface);
}

static void *virtio_rng_device_create(void *virtio_dev,
                                      QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVirtioRngDevice *virtio_rdevice = g_new0(QVirtioRngDevice, 1);
    QVirtioRng *interface = &virtio_rdevice->rng;

    interface->vdev = virtio_dev;

    virtio_rdevice->obj.get_driver = qvirtio_rng_device_get_driver;

    return &virtio_rdevice->obj;
}

/* virtio-rng-pci */
static void *qvirtio_rng_pci_get_driver(void *object, const char *interface)
{
    QVirtioRngPCI *v_rng = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_rng->pci_vdev.pdev;
    }
    return qvirtio_rng_get_driver(&v_rng->rng, interface);
}

static void *virtio_rng_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                   void *addr)
{
    QVirtioRngPCI *virtio_rpci = g_new0(QVirtioRngPCI, 1);
    QVirtioRng *interface = &virtio_rpci->rng;
    QOSGraphObject *obj = &virtio_rpci->pci_vdev.obj;

    virtio_pci_init(&virtio_rpci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_rpci->pci_vdev.vdev;

    obj->get_driver = qvirtio_rng_pci_get_driver;

    return obj;
}

static void virtio_rng_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    /* virtio-rng-device */
    qos_node_create_driver("virtio-rng-device", virtio_rng_device_create);
    qos_node_consumes("virtio-rng-device", "virtio-bus", NULL);
    qos_node_produces("virtio-rng-device", "virtio");
    qos_node_produces("virtio-rng-device", "virtio-rng");

    /* virtio-rng-pci */
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-rng-pci", virtio_rng_pci_create);
    qos_node_consumes("virtio-rng-pci", "pci-bus", &opts);
    qos_node_produces("virtio-rng-pci", "pci-device");
    qos_node_produces("virtio-rng-pci", "virtio");
    qos_node_produces("virtio-rng-pci", "virtio-rng");
}

libqos_init(virtio_rng_register_nodes);
