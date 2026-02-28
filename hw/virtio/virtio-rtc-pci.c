/*
 * Virtio RTC PCI Bindings
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-rtc.h"
#include "standard-headers/linux/virtio_ids.h"

typedef struct VirtIORtcPCI {
    VirtIOPCIProxy parent_obj;
    VirtIORtc vdev;
} VirtIORtcPCI;

#define TYPE_VIRTIO_RTC_PCI "virtio-rtc-pci-base"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIORtcPCI, VIRTIO_RTC_PCI)

static void virtio_rtc_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIORtcPCI *dev = VIRTIO_RTC_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_rtc_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    k->realize = virtio_rtc_pci_realize;

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_10_BASE + VIRTIO_ID_CLOCK;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_SYSTEM_RTC;
}

static void virtio_rtc_pci_instance_init(Object *obj)
{
    VirtIORtcPCI *dev = VIRTIO_RTC_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                      TYPE_VIRTIO_RTC);
}

static const VirtioPCIDeviceTypeInfo virtio_rtc_pci_info = {
    .base_name = TYPE_VIRTIO_RTC_PCI,
    .non_transitional_name = "virtio-rtc-pci",
    .instance_size = sizeof(VirtIORtcPCI),
    .instance_init = virtio_rtc_pci_instance_init,
    .class_init = virtio_rtc_pci_class_init,
};

static void virtio_rtc_pci_register(void)
{
    virtio_pci_types_register(&virtio_rtc_pci_info);
}

type_init(virtio_rtc_pci_register);
