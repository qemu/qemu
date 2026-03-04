/*
 * Vhost-user generic virtio device PCI glue
 *
 * Copyright (c) 2023 Linaro Ltd
 * Author: Alex Bennée <alex.bennee@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "hw/virtio/vhost-user-base.h"
#include "hw/virtio/virtio-pci.h"

#define VIRTIO_DEVICE_PCI_SHMEM_BAR 4

struct VHostUserDevicePCI {
    VirtIOPCIProxy parent_obj;

    VHostUserBase vub;
    MemoryRegion shmembar;
};

#define TYPE_VHOST_USER_TEST_DEVICE_PCI "vhost-user-test-device-pci-base"

OBJECT_DECLARE_SIMPLE_TYPE(VHostUserDevicePCI, VHOST_USER_TEST_DEVICE_PCI)

static void vhost_user_device_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserDevicePCI *dev = VHOST_USER_TEST_DEVICE_PCI(vpci_dev);
    DeviceState *dev_state = DEVICE(&dev->vub);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev_state);
    VirtioSharedMemory *shmem;
    uint64_t offset = 0, shmem_size = 0;

    /* Keep modern 64-bit BARs (2 slots) away from the shared memory BAR. */
    vpci_dev->modern_mem_bar_idx = 2;
    vpci_dev->nvectors = 1;
    if (!qdev_realize(dev_state, BUS(&vpci_dev->bus), errp)) {
        return;
    }

    QSIMPLEQ_FOREACH(shmem, &vdev->shmem_list, entry) {
        if (memory_region_size(&shmem->mr) > UINT64_MAX - shmem_size) {
            error_setg(errp, "Total shared memory required overflow");
            return;
        }
        shmem_size = shmem_size + memory_region_size(&shmem->mr);
    }
    if (shmem_size) {
        if (vpci_dev->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY) {
            error_setg(errp, "modern-pio-notify is not supported due to PCI BAR layout limitations");
            return;
        }
        memory_region_init(&dev->shmembar, OBJECT(vpci_dev),
                           "vhost-device-pci-shmembar", shmem_size);
        QSIMPLEQ_FOREACH(shmem, &vdev->shmem_list, entry) {
            memory_region_add_subregion(&dev->shmembar, offset, &shmem->mr);
            virtio_pci_add_shm_cap(vpci_dev, VIRTIO_DEVICE_PCI_SHMEM_BAR,
                                   offset, memory_region_size(&shmem->mr),
                                   shmem->shmid);
            offset = offset + memory_region_size(&shmem->mr);
        }
        pci_register_bar(&vpci_dev->pci_dev, VIRTIO_DEVICE_PCI_SHMEM_BAR,
                        PCI_BASE_ADDRESS_SPACE_MEMORY |
                        PCI_BASE_ADDRESS_MEM_PREFETCH |
                        PCI_BASE_ADDRESS_MEM_TYPE_64,
                        &dev->shmembar);
    }
}

static void vhost_user_device_pci_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = vhost_user_device_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = 0; /* Set by virtio-pci based on virtio id */
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_user_device_pci_instance_init(Object *obj)
{
    VHostUserDevicePCI *dev = VHOST_USER_TEST_DEVICE_PCI(obj);

    virtio_instance_init_common(obj, &dev->vub, sizeof(dev->vub),
                                TYPE_VHOST_USER_TEST_DEVICE);
}

static const VirtioPCIDeviceTypeInfo vhost_user_device_pci_info = {
    .base_name = TYPE_VHOST_USER_TEST_DEVICE_PCI,
    .non_transitional_name = "vhost-user-test-device-pci",
    .instance_size = sizeof(VHostUserDevicePCI),
    .instance_init = vhost_user_device_pci_instance_init,
    .class_init = vhost_user_device_pci_class_init,
};

static void vhost_user_device_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_device_pci_info);
}

type_init(vhost_user_device_pci_register);
