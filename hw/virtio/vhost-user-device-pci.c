/*
 * Vhost-user generic virtio device PCI glue
 *
 * Copyright (c) 2023 Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-base.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserDevicePCI {
    VirtIOPCIProxy parent_obj;

    VHostUserBase vub;
};

#define TYPE_VHOST_USER_DEVICE_PCI "vhost-user-device-pci-base"

OBJECT_DECLARE_SIMPLE_TYPE(VHostUserDevicePCI, VHOST_USER_DEVICE_PCI)

static void vhost_user_device_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserDevicePCI *dev = VHOST_USER_DEVICE_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vub);

    vpci_dev->nvectors = 1;
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_device_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    /* Reason: stop users confusing themselves */
    dc->user_creatable = false;

    k->realize = vhost_user_device_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_user_device_pci_instance_init(Object *obj)
{
    VHostUserDevicePCI *dev = VHOST_USER_DEVICE_PCI(obj);

    virtio_instance_init_common(obj, &dev->vub, sizeof(dev->vub),
                                TYPE_VHOST_USER_DEVICE);
}

static const VirtioPCIDeviceTypeInfo vhost_user_device_pci_info = {
    .base_name = TYPE_VHOST_USER_DEVICE_PCI,
    .non_transitional_name = "vhost-user-device-pci",
    .instance_size = sizeof(VHostUserDevicePCI),
    .instance_init = vhost_user_device_pci_instance_init,
    .class_init = vhost_user_device_pci_class_init,
};

static void vhost_user_device_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_device_pci_info);
}

type_init(vhost_user_device_pci_register);
