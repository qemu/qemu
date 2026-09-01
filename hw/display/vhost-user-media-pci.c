/*
 * Vhost-user MEDIA virtio device PCI glue
 *
 * Copyright Red Hat, Inc. 2026
 * Authors: Albert Esteve <aesteve@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/virtio/vhost-user-media.h"
#include "hw/virtio/virtio-pci.h"

/* BAR 2 is used for the shared memory region exposed to the guest */
#define VIRTIO_MEDIA_PCI_SHM_BAR 2

#define VIRTIO_MEDIA_PCI_SHMCAP_ID_SHM 0

#define TYPE_VHOST_USER_MEDIA_PCI "vhost-user-media-pci-base"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserMEDIAPCI, VHOST_USER_MEDIA_PCI)

struct VHostUserMEDIAPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserMEDIA vdev;
    MemoryRegion shmbar;
};

static const Property vumedia_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
};

static void vumedia_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserMEDIAPCI *dev = VHOST_USER_MEDIA_PCI(vpci_dev);
    DeviceState *dev_state = DEVICE(&dev->vdev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev_state);
    VirtioSharedMemory *shmem;
    uint64_t shm_size;

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = 1;
    }

    if (vpci_dev->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY) {
        error_setg(errp, "modern-pio-notify is not supported due to PCI BAR "
                         "layout limitations");
        return;
    }

    if (!qdev_realize(dev_state, BUS(&vpci_dev->bus), errp)) {
        return;
    }

    shmem = virtio_find_shmem_region(vdev, VIRTIO_MEDIA_PCI_SHMCAP_ID_SHM);
    if (!shmem) {
        error_setg(errp, "vhost-user-media: missing shared memory region");
        return;
    }
    shm_size = memory_region_size(&shmem->mr);

    memory_region_init(&dev->shmbar, OBJECT(vpci_dev),
                       "vhost-media-pci-shmbar", shm_size);
    memory_region_add_subregion(&dev->shmbar, 0, &shmem->mr);
    virtio_pci_add_shm_cap(vpci_dev, VIRTIO_MEDIA_PCI_SHM_BAR, 0,
                           shm_size, VIRTIO_MEDIA_PCI_SHMCAP_ID_SHM);

    /* After 'realized' so the memory region exists */
    pci_register_bar(&vpci_dev->pci_dev, VIRTIO_MEDIA_PCI_SHM_BAR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_PREFETCH |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &dev->shmbar);
}

static void vumedia_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vumedia_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vumedia_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_MULTIMEDIA_VIDEO;
}

static void vumedia_pci_instance_init(Object *obj)
{
    VHostUserMEDIAPCI *dev = VHOST_USER_MEDIA_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_MEDIA);
}

static const VirtioPCIDeviceTypeInfo vumedia_pci_info = {
    .base_name             = TYPE_VHOST_USER_MEDIA_PCI,
    .non_transitional_name = "vhost-user-media-pci",
    .instance_size = sizeof(VHostUserMEDIAPCI),
    .instance_init = vumedia_pci_instance_init,
    .class_init    = vumedia_pci_class_init,
};

static void vumedia_pci_register(void)
{
    virtio_pci_types_register(&vumedia_pci_info);
}

type_init(vumedia_pci_register);
