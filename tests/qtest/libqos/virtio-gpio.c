/*
 * virtio-gpio nodes for testing
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_config.h"
#include "../libqtest.h"
#include "qemu/module.h"
#include "qgraph.h"
#include "virtio-gpio.h"

static QGuestAllocator *alloc;

static void virtio_gpio_cleanup(QVhostUserGPIO *gpio)
{
    QVirtioDevice *vdev = gpio->vdev;
    int i;

    for (i = 0; i < 2; i++) {
        qvirtqueue_cleanup(vdev->bus, gpio->queues[i], alloc);
    }
    g_free(gpio->queues);
}

/*
 * This handles the VirtIO setup from the point of view of the driver
 * frontend and therefor doesn't present any vhost specific features
 * and in fact masks of the re-used bit.
 */
static void virtio_gpio_setup(QVhostUserGPIO *gpio)
{
    QVirtioDevice *vdev = gpio->vdev;
    uint64_t features;
    int i;

    features = qvirtio_get_features(vdev);
    features &= ~QVIRTIO_F_BAD_FEATURE;
    qvirtio_set_features(vdev, features);

    gpio->queues = g_new(QVirtQueue *, 2);
    for (i = 0; i < 2; i++) {
        gpio->queues[i] = qvirtqueue_setup(vdev, alloc, i);
    }
    qvirtio_set_driver_ok(vdev);
}

static void *qvirtio_gpio_get_driver(QVhostUserGPIO *v_gpio,
                                     const char *interface)
{
    if (!g_strcmp0(interface, "vhost-user-gpio")) {
        return v_gpio;
    }
    if (!g_strcmp0(interface, "virtio")) {
        return v_gpio->vdev;
    }

    g_assert_not_reached();
}

static void *qvirtio_gpio_device_get_driver(void *object,
                                            const char *interface)
{
    QVhostUserGPIODevice *v_gpio = object;
    return qvirtio_gpio_get_driver(&v_gpio->gpio, interface);
}

/* virtio-gpio (mmio) */
static void qvirtio_gpio_device_destructor(QOSGraphObject *obj)
{
    QVhostUserGPIODevice *gpio_dev = (QVhostUserGPIODevice *) obj;
    virtio_gpio_cleanup(&gpio_dev->gpio);
}

static void qvirtio_gpio_device_start_hw(QOSGraphObject *obj)
{
    QVhostUserGPIODevice *gpio_dev = (QVhostUserGPIODevice *) obj;
    virtio_gpio_setup(&gpio_dev->gpio);
}

static void *virtio_gpio_device_create(void *virtio_dev,
                                       QGuestAllocator *t_alloc,
                                       void *addr)
{
    QVhostUserGPIODevice *virtio_device = g_new0(QVhostUserGPIODevice, 1);
    QVhostUserGPIO *interface = &virtio_device->gpio;

    interface->vdev = virtio_dev;
    alloc = t_alloc;

    virtio_device->obj.get_driver = qvirtio_gpio_device_get_driver;
    virtio_device->obj.start_hw = qvirtio_gpio_device_start_hw;
    virtio_device->obj.destructor = qvirtio_gpio_device_destructor;

    return &virtio_device->obj;
}

/* virtio-gpio-pci */
static void qvirtio_gpio_pci_destructor(QOSGraphObject *obj)
{
    QVhostUserGPIOPCI *gpio_pci = (QVhostUserGPIOPCI *) obj;
    QOSGraphObject *pci_vobj =  &gpio_pci->pci_vdev.obj;

    virtio_gpio_cleanup(&gpio_pci->gpio);
    qvirtio_pci_destructor(pci_vobj);
}

static void qvirtio_gpio_pci_start_hw(QOSGraphObject *obj)
{
    QVhostUserGPIOPCI *gpio_pci = (QVhostUserGPIOPCI *) obj;
    QOSGraphObject *pci_vobj =  &gpio_pci->pci_vdev.obj;

    qvirtio_pci_start_hw(pci_vobj);
    virtio_gpio_setup(&gpio_pci->gpio);
}

static void *qvirtio_gpio_pci_get_driver(void *object, const char *interface)
{
    QVhostUserGPIOPCI *v_gpio = object;

    if (!g_strcmp0(interface, "pci-device")) {
        return v_gpio->pci_vdev.pdev;
    }
    return qvirtio_gpio_get_driver(&v_gpio->gpio, interface);
}

static void *virtio_gpio_pci_create(void *pci_bus, QGuestAllocator *t_alloc,
                                    void *addr)
{
    QVhostUserGPIOPCI *virtio_spci = g_new0(QVhostUserGPIOPCI, 1);
    QVhostUserGPIO *interface = &virtio_spci->gpio;
    QOSGraphObject *obj = &virtio_spci->pci_vdev.obj;

    virtio_pci_init(&virtio_spci->pci_vdev, pci_bus, addr);
    interface->vdev = &virtio_spci->pci_vdev.vdev;
    alloc = t_alloc;

    obj->get_driver = qvirtio_gpio_pci_get_driver;
    obj->start_hw = qvirtio_gpio_pci_start_hw;
    obj->destructor = qvirtio_gpio_pci_destructor;

    return obj;
}

static void virtio_gpio_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
    };

    QOSGraphEdgeOptions edge_opts = { };

    /* vhost-user-gpio-device */
    edge_opts.extra_device_opts = "id=gpio0,chardev=chr-vhost-user-test "
        "-global virtio-mmio.force-legacy=false";
    qos_node_create_driver("vhost-user-gpio-device",
                            virtio_gpio_device_create);
    qos_node_consumes("vhost-user-gpio-device", "virtio-bus", &edge_opts);
    qos_node_produces("vhost-user-gpio-device", "vhost-user-gpio");

    /* virtio-gpio-pci */
    edge_opts.extra_device_opts = "id=gpio0,addr=04.0,chardev=chr-vhost-user-test";
    add_qpci_address(&edge_opts, &addr);
    qos_node_create_driver("vhost-user-gpio-pci", virtio_gpio_pci_create);
    qos_node_consumes("vhost-user-gpio-pci", "pci-bus", &edge_opts);
    qos_node_produces("vhost-user-gpio-pci", "vhost-user-gpio");
}

libqos_init(virtio_gpio_register_nodes);
