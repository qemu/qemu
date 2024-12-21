/*
 * Vhost-user RNG virtio device PCI glue
 *
 * Copyright (c) 2021 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-rng.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserRNGPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserRNG vdev;
};

typedef struct VHostUserRNGPCI VHostUserRNGPCI;

#define TYPE_VHOST_USER_RNG_PCI "vhost-user-rng-pci-base"

DECLARE_INSTANCE_CHECKER(VHostUserRNGPCI, VHOST_USER_RNG_PCI,
                         TYPE_VHOST_USER_RNG_PCI)

static const Property vhost_user_rng_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
};

static void vhost_user_rng_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserRNGPCI *dev = VHOST_USER_RNG_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = 1;
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_rng_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_rng_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    device_class_set_props(dc, vhost_user_rng_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void vhost_user_rng_pci_instance_init(Object *obj)
{
    VHostUserRNGPCI *dev = VHOST_USER_RNG_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_RNG);
}

static const VirtioPCIDeviceTypeInfo vhost_user_rng_pci_info = {
    .base_name = TYPE_VHOST_USER_RNG_PCI,
    .non_transitional_name = "vhost-user-rng-pci",
    .instance_size = sizeof(VHostUserRNGPCI),
    .instance_init = vhost_user_rng_pci_instance_init,
    .class_init = vhost_user_rng_pci_class_init,
};

static void vhost_user_rng_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_rng_pci_info);
}

type_init(vhost_user_rng_pci_register);
