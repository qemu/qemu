/*
 * virtio-scmi nodes for testing
 *
 * Copyright (c) Linaro Ltd.
 * SPDX-FileCopyrightText: Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on virtio-gpio.c, doing basically the same thing.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_config.h"
#include "../libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "virtio-scmi.h"

static QGuestAllocator *alloc;

static void virtio_scmi_cleanup(QVhostUserSCMI *scmi)
{
    QVirtioDevice *vdev = scmi->vdev;
    int i;

    for (i = 0; i < 2; i++) {
        qvirtqueue_cleanup(vdev->bus, scmi->queues[i], alloc);
    }
    g_free(scmi->queues);
}

/*
 * This handles the VirtIO setup from the point of view of the driver
 * frontend and therefore doesn't present any vhost specific features
 * and in fact masks of the re-used bit.
 */
static void virtio_scmi_setup(QVhostUserSCMI *scmi)
{
    QVirtioDevice *vdev = scmi->vdev;
    uint64_t features;
    int i;

    features = qvirtio_get_features(vdev);
    features &= ~QVIRTIO_F_BAD_FEATURE;
    qvirtio_set_features(vdev, features);

    scmi->queues = g_new(QVirtQueue *, 2);
    for (i = 0; i < 2; i++) {
        scmi->queues[i] = qvirtqueue_setup(vdev, alloc, i);
    }
    qvirtio_set_driver_ok(vdev);
}

static void *qvirtio_scmi_get_driver(QVhostUserSCMI *v_scmi,
                                     const char *interface)
{
    if (!g_strcmp0(interface, "vhost-user-scmi")) {
        return v_scmi;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_scmi->vdev;
    }

    g_assert_not_reached();
}

static void *qvirtio_scmi_device_get_driver(void *object,
                                            const char *interface)
{
    QVhostUserSCMIDevice *v_scmi = object;
    return qvirtio_scmi_get_driver(&v_scmi->scmi, interface);
}

/* virtio-scmi (mmio) */
static void qvirtio_scmi_device_destructor(QOSGraphObject *obj)
{
    QVhostUserSCMIDevice *scmi_dev = (QVhostUserSCMIDevice *) obj;
    virtio_scmi_cleanup(&scmi_dev->scmi);
}

static void qvirtio_scmi_device_start_hw(QOSGraphObject *obj)
{
    QVhostUserSCMIDevice *scmi_dev = (QVhostUserSCMIDevice *) obj;
    virtio_scmi_setup(&scmi_dev->scmi);
}

static void *virtio_scmi_device_create(void *virtio_dev,
                                       QGuestAllocator *t_alloc,
                                       void *addr)
{
    QVhostUserSCMIDevice *virtio_device = g_new0(QVhostUserSCMIDevice, 1);
    QVhostUserSCMI *interface = &virtio_device->scmi;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_device->obj.get_driver = qvirtio_scmi_device_get_driver;
    virtio_device->obj.start_hw = qvirtio_scmi_device_start_hw;
    virtio_device->obj.destructor = qvirtio_scmi_device_destructor;

    return &virtio_device->obj;
}

/* virtio-scmi-pci */
static void qvirtio_scmi_pci_destructor(QOSGraphObject *obj)
{
    QVhostUserSCMIPCI *scmi_pci = (QVhostUserSCMIPCI *) obj;
    QOSGraphObject *pci_vobj =  &scmi_pci->pci_vdev.obj;

    virtio_scmi_cleanup(&scmi_pci->scmi);
    qvirtio_pci_destructor(pci_vobj);
}

static void qvirtio_scmi_pci_start_hw(QOSGraphObject *obj)
{
    QVhostUserSCMIPCI *scmi_pci = (QVhostUserSCMIPCI *) obj;
    QOSGraphObject *pci_vobj =  &scmi_pci->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_scmi_setup(&scmi_pci->scmi);
}

static void *qvirtio_scmi_pci_get_driver(void *object, const char *interface)
{
    QVhostUserSCMIPCI *v_scmi = object;

    if (!g_strcmp0(interface, "pci-device")) {
        return v_scmi->pci_vdev.pdev;
    }
    return qvirtio_scmi_get_driver(&v_scmi->scmi, interface);
}

static void *virtio_scmi_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                    void *addr)
{
    QVhostUserSCMIPCI *virtio_spci = g_new0(QVhostUserSCMIPCI, 1);
    QVhostUserSCMI *interface = &virtio_spci->scmi;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;
    alloc = t_alloc;

    obj->get_driver = qvirtio_scmi_pci_get_driver;
    obj->start_hw = qvirtio_scmi_pci_start_hw;
    obj->destructor = qvirtio_scmi_pci_destructor;

    return obj;
}

static void virtio_scmi_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions edge_opts = { };

    /* vhost-user-scmi-device */
    edge_opts.extra_device_opts = "id=scmi,chardev=chr-vhost-user-test "
        "-global virtio-mmio.force-legacy=false";
    qos_node_create_driver("vhost-user-scmi-device",
                            virtio_scmi_device_create);
    qos_node_consumes("vhost-user-scmi-device", "virtio-bus", &edge_opts);
    qos_node_produces("vhost-user-scmi-device", "vhost-user-scmi");

    /* virtio-scmi-pci */
    edge_opts.extra_device_opts = "id=scmi,addr=04.0,chardev=chr-vhost-user-test";
    add_qpci_address(&edge_opts, &addr);
    qos_node_create_driver("vhost-user-scmi-pci", virtio_scmi_pci_create);
    qos_node_consumes("vhost-user-scmi-pci", "pci-bus", &edge_opts);
    qos_node_produces("vhost-user-scmi-pci", "vhost-user-scmi");
}

libqos_init(virtio_scmi_register_nodes);
