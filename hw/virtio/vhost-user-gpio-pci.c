/*
 * Vhost-user gpio virtio device PCI glue
 *
 * Copyright (c) 2022 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-gpio.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserGPIOPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserGPIO vdev;
};

typedef struct VHostUserGPIOPCI VHostUserGPIOPCI;

#define TYPE_VHOST_USER_GPIO_PCI "vhost-user-gpio-pci-base"

DECLARE_INSTANCE_CHECKER(VHostUserGPIOPCI, VHOST_USER_GPIO_PCI,
                         TYPE_VHOST_USER_GPIO_PCI)

static void vhost_user_gpio_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserGPIOPCI *dev = VHOST_USER_GPIO_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    vpci_dev->nvectors = 1;
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_gpio_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_gpio_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_user_gpio_pci_instance_init(Object *obj)
{
    VHostUserGPIOPCI *dev = VHOST_USER_GPIO_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_GPIO);
}

static const VirtioPCIDeviceTypeInfo vhost_user_gpio_pci_info = {
    .base_name = TYPE_VHOST_USER_GPIO_PCI,
    .non_transitional_name = "vhost-user-gpio-pci",
    .instance_size = sizeof(VHostUserGPIOPCI),
    .instance_init = vhost_user_gpio_pci_instance_init,
    .class_init = vhost_user_gpio_pci_class_init,
};

static void vhost_user_gpio_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_gpio_pci_info);
}

type_init(vhost_user_gpio_pci_register);
