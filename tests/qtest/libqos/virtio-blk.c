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
#include "standard-headers/linux/virtio_blk.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-blk.h"

#define PCI_SLOT                0x04
#define PCI_FN                  0x00

/* virtio-blk-device */
static void *qvirtio_blk_get_driver(QVirtioBlk *v_blk,
                                    const char *interface)
{
    if (!g_strcmp0(interface, "virtio-blk")) {
        return v_blk;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_blk->vdev;
    }

    fprintf(stderr, "%s not present in virtio-blk-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_blk_device_get_driver(void *object,
                                           const char *interface)
{
    QVirtioBlkDevice *v_blk = object;
    return qvirtio_blk_get_driver(&v_blk->blk, interface);
}

static void *virtio_blk_device_create(void *virtio_dev,
                                      QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVirtioBlkDevice *virtio_blk = g_new0(QVirtioBlkDevice, 1);
    QVirtioBlk *interface = &virtio_blk->blk;

    interface->vdev = virtio_dev;

    virtio_blk->obj.get_driver = qvirtio_blk_device_get_driver;

    return &virtio_blk->obj;
}

/* virtio-blk-pci */
static void *qvirtio_blk_pci_get_driver(void *object, const char *interface)
{
    QVirtioBlkPCI *v_blk = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_blk->pci_vdev.pdev;
    }
    return qvirtio_blk_get_driver(&v_blk->blk, interface);
}

static void *virtio_blk_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVirtioBlkPCI *virtio_blk = g_new0(QVirtioBlkPCI, 1);
    QVirtioBlk *interface = &virtio_blk->blk;
    QOSGraphObject *obj = &virtio_blk->pci_vdev.obj;

    virtio_pci_init(&virtio_blk->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_blk->pci_vdev.vdev;

    g_assert_cmphex(interface->vdev->device_type, ==, VIRTIO_ID_BLOCK);

    obj->get_driver = qvirtio_blk_pci_get_driver;

    return obj;
}

static void virtio_blk_register_nodes(void)
{
    /* FIXME: every test using these two nodes needs to setup a
     * -drive,id=drive0 otherwise QEMU is not going to start.
     * Therefore, we do not include "produces" edge for virtio
     * and pci-device yet.
    */

    char *arg = g_strdup_printf("id=drv0,drive=drive0,addr=%x.%x",
                                PCI_SLOT, PCI_FN);

    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(PCI_SLOT, PCI_FN),
    };

    QOSGraphEdgeOptions opts = { };

    /* virtio-blk-device */
    opts.extra_device_opts = "drive=drive0";
    qos_node_create_driver("virtio-blk-device", virtio_blk_device_create);
    qos_node_consumes("virtio-blk-device", "virtio-bus", &opts);
    qos_node_produces("virtio-blk-device", "virtio-blk");

    /* virtio-blk-pci */
    opts.extra_device_opts = arg;
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-blk-pci", virtio_blk_pci_create);
    qos_node_consumes("virtio-blk-pci", "pci-bus", &opts);
    qos_node_produces("virtio-blk-pci", "virtio-blk");

    g_free(arg);
}

libqos_init(virtio_blk_register_nodes);
