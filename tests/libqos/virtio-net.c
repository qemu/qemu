/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
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
#include "libqos/qgraph.h"
#include "libqos/virtio-net.h"
#include "hw/virtio/virtio-net.h"


static QGuestAllocator *alloc;

static void virtio_net_cleanup(QVirtioNet *interface)
{
    int i;

    for (i = 0; i < interface->n_queues; i++) {
        qvirtqueue_cleanup(interface->vdev->bus, interface->queues[i], alloc);
    }
    g_free(interface->queues);
}

static void virtio_net_setup(QVirtioNet *interface)
{
    QVirtioDevice *vdev = interface->vdev;
    uint64_t features;
    int i;

    features = qvirtio_get_features(vdev);
    features &= ~(QVIRTIO_F_BAD_FEATURE |
                  (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                  (1ull << VIRTIO_RING_F_EVENT_IDX));
    qvirtio_set_features(vdev, features);

    if (features & (1ull << VIRTIO_NET_F_MQ)) {
        interface->n_queues = qvirtio_config_readw(vdev, 8) * 2;
    } else {
        interface->n_queues = 2;
    }
    interface->n_queues++; /* Account for the ctrl queue */

    interface->queues = g_new(QVirtQueue *, interface->n_queues);
    for (i = 0; i < interface->n_queues; i++) {
        interface->queues[i] = qvirtqueue_setup(vdev, alloc, i);
    }
    qvirtio_set_driver_ok(vdev);
}

/* virtio-net-device */
static void qvirtio_net_device_destructor(QOSGraphObject *obj)
{
    QVirtioNetDevice *v_net = (QVirtioNetDevice *) obj;
    virtio_net_cleanup(&v_net->net);
}

static void qvirtio_net_device_start_hw(QOSGraphObject *obj)
{
    QVirtioNetDevice *v_net = (QVirtioNetDevice *) obj;
    QVirtioNet *interface = &v_net->net;

    virtio_net_setup(interface);
}

static void *qvirtio_net_get_driver(QVirtioNet *v_net,
                                    const char *interface)
{
    if (!g_strcmp0(interface, "virtio-net")) {
        return v_net;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_net->vdev;
    }

    fprintf(stderr, "%s not present in virtio-net-device\n", interface);
    g_assert_not_reached();
}

static void *qvirtio_net_device_get_driver(void *object,
                                           const char *interface)
{
    QVirtioNetDevice *v_net = object;
    return qvirtio_net_get_driver(&v_net->net, interface);
}

static void *virtio_net_device_create(void *virtio_dev,
                                          QGuestAllocator *t_alloc,
                                          void *addr)
{
    QVirtioNetDevice *virtio_ndevice = g_new0(QVirtioNetDevice, 1);
    QVirtioNet *interface = &virtio_ndevice->net;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_ndevice->obj.destructor = qvirtio_net_device_destructor;
    virtio_ndevice->obj.get_driver = qvirtio_net_device_get_driver;
    virtio_ndevice->obj.start_hw = qvirtio_net_device_start_hw;

    return &virtio_ndevice->obj;
}

/* virtio-net-pci */
static void qvirtio_net_pci_destructor(QOSGraphObject *obj)
{
    QVirtioNetPCI *v_net = (QVirtioNetPCI *) obj;
    QVirtioNet *interface = &v_net->net;
    QOSGraphObject *pci_vobj =  &v_net->pci_vdev.obj;

    virtio_net_cleanup(interface);
    qvirtio_pci_destructor(pci_vobj);
}

static void qvirtio_net_pci_start_hw(QOSGraphObject *obj)
{
    QVirtioNetPCI *v_net = (QVirtioNetPCI *) obj;
    QVirtioNet *interface = &v_net->net;
    QOSGraphObject *pci_vobj =  &v_net->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_net_setup(interface);
}

static void *qvirtio_net_pci_get_driver(void *object,
                                            const char *interface)
{
    QVirtioNetPCI *v_net = object;
    if (!g_strcmp0(interface, "pci-device")) {
        return v_net->pci_vdev.pdev;
    }
    return qvirtio_net_get_driver(&v_net->net, interface);
}

static void *virtio_net_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                  void *addr)
{
    QVirtioNetPCI *virtio_bpci = g_new0(QVirtioNetPCI, 1);
    QVirtioNet *interface = &virtio_bpci->net;
    QOSGraphObject *obj = &virtio_bpci->pci_vdev.obj;

    virtio_pci_init(&virtio_bpci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_bpci->pci_vdev.vdev;
    alloc = t_alloc;

    g_assert_cmphex(interface->vdev->device_type, ==, VIRTIO_ID_NET);

    obj->destructor = qvirtio_net_pci_destructor;
    obj->start_hw = qvirtio_net_pci_start_hw;
    obj->get_driver = qvirtio_net_pci_get_driver;

    return obj;
}

static void virtio_net_register_nodes(void)
{
    /* FIXME: every test using these nodes needs to setup a
     * -netdev socket,id=hs0 otherwise QEMU is not going to start.
     * Therefore, we do not include "produces" edge for virtio
     * and pci-device yet.
     */
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions opts = { };

    /* virtio-net-device */
    opts.extra_device_opts = "netdev=hs0";
    qos_node_create_driver("virtio-net-device",
                            virtio_net_device_create);
    qos_node_consumes("virtio-net-device", "virtio-bus", &opts);
    qos_node_produces("virtio-net-device", "virtio-net");

    /* virtio-net-pci */
    opts.extra_device_opts = "netdev=hs0,addr=04.0";
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("virtio-net-pci", virtio_net_pci_create);
    qos_node_consumes("virtio-net-pci", "pci-bus", &opts);
    qos_node_produces("virtio-net-pci", "virtio-net");
}

libqos_init(virtio_net_register_nodes);
