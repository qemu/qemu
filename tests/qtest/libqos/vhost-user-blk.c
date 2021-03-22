/*
 * libqos driver framework
 *
 * Based on tests/qtest/libqos/virtio-blk.c
 *
 * Copyright (c) 2020 Coiby Xu <coiby.xu@gmail.com>
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
#include "vhost-user-blk.h"

#define PCI_SLOT                0x04
#define PCI_FN                  0x00

/* virtio-blk-device */
static void *qvhost_user_blk_get_driver(QVhostUserBlk *v_blk,
                                    const char *interface)
{
    if (!g_strcmp0(interface, "vhost-user-blk")) {
        return v_blk;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_blk->vdev;
    }

    fprintf(stderr, "%s not present in vhost-user-blk-device\n", interface);
    g_assert_not_reached();
}

static void *qvhost_user_blk_device_get_driver(void *object,
                                           const char *interface)
{
    QVhostUserBlkDevice *v_blk = object;
    return qvhost_user_blk_get_driver(&v_blk->blk, interface);
}

static void *vhost_user_blk_device_create(void *virtio_dev,
                                      QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVhostUserBlkDevice *vhost_user_blk = g_new0(QVhostUserBlkDevice, 1);
    QVhostUserBlk *interface = &vhost_user_blk->blk;

    interface->vdev = virtio_dev;

    vhost_user_blk->obj.get_driver = qvhost_user_blk_device_get_driver;

    return &vhost_user_blk->obj;
}

/* virtio-blk-pci */
static void *qvhost_user_blk_pci_get_driver(void *object, const char *interface)
{
    QVhostUserBlkPCI *v_blk = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_blk->pci_vdev.pdev;
    }
    return qvhost_user_blk_get_driver(&v_blk->blk, interface);
}

static void *vhost_user_blk_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                      void *addr)
{
    QVhostUserBlkPCI *vhost_user_blk = g_new0(QVhostUserBlkPCI, 1);
    QVhostUserBlk *interface = &vhost_user_blk->blk;
    QOSGraphObject *obj = &vhost_user_blk->pci_vdev.obj;

    virtio_pci_init(&vhost_user_blk->pci_vdev, pci_bus, addr);
    interface->vdev = &vhost_user_blk->pci_vdev.vdev;

    g_assert_cmphex(interface->vdev->device_type, ==, VIRTIO_ID_BLOCK);

    obj->get_driver = qvhost_user_blk_pci_get_driver;

    return obj;
}

static void vhost_user_blk_register_nodes(void)
{
    /*
     * FIXME: every test using these two nodes needs to setup a
     * -drive,id=drive0 otherwise QEMU is not going to start.
     * Therefore, we do not include "produces" edge for virtio
     * and pci-device yet.
     */

    char *arg = g_strdup_printf("id=drv0,chardev=char1,addr=%x.%x",
                                PCI_SLOT, PCI_FN);

    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(PCI_SLOT, PCI_FN),
    };

    QOSGraphEdgeOptions opts = { };

    /* virtio-blk-device */
    /** opts.extra_device_opts = "drive=drive0"; */
    qos_node_create_driver("vhost-user-blk-device",
                           vhost_user_blk_device_create);
    qos_node_consumes("vhost-user-blk-device", "virtio-bus", &opts);
    qos_node_produces("vhost-user-blk-device", "vhost-user-blk");

    /* virtio-blk-pci */
    opts.extra_device_opts = arg;
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("vhost-user-blk-pci", vhost_user_blk_pci_create);
    qos_node_consumes("vhost-user-blk-pci", "pci-bus", &opts);
    qos_node_produces("vhost-user-blk-pci", "vhost-user-blk");

    g_free(arg);
}

libqos_init(vhost_user_blk_register_nodes);
