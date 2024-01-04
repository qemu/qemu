/*
 * Vhost-user Sound virtio device PCI glue
 *
 * Copyright (c) 2023 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-snd.h"
#include "hw/virtio/virtio-pci.h"

struct VHostUserSoundPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserSound vdev;
};

typedef struct VHostUserSoundPCI VHostUserSoundPCI;

#define TYPE_VHOST_USER_SND_PCI "vhost-user-snd-pci-base"

DECLARE_INSTANCE_CHECKER(VHostUserSoundPCI, VHOST_USER_SND_PCI,
                         TYPE_VHOST_USER_SND_PCI)

static Property vhost_user_snd_pci_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_snd_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserSoundPCI *dev = VHOST_USER_SND_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    vpci_dev->nvectors = 1;

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_snd_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_snd_pci_realize;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    device_class_set_props(dc, vhost_user_snd_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
}

static void vhost_user_snd_pci_instance_init(Object *obj)
{
    VHostUserSoundPCI *dev = VHOST_USER_SND_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_SND);
}

static const VirtioPCIDeviceTypeInfo vhost_user_snd_pci_info = {
    .base_name = TYPE_VHOST_USER_SND_PCI,
    .non_transitional_name = "vhost-user-snd-pci",
    .instance_size = sizeof(VHostUserSoundPCI),
    .instance_init = vhost_user_snd_pci_instance_init,
    .class_init = vhost_user_snd_pci_class_init,
};

static void vhost_user_snd_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_snd_pci_info);
}

type_init(vhost_user_snd_pci_register);
