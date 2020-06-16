/*
 * Vhost-user vsock PCI Bindings
 *
 * Copyright 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-user-vsock.h"

typedef struct VHostUserVSockPCI VHostUserVSockPCI;

/*
 * vhost-user-vsock-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_USER_VSOCK_PCI "vhost-user-vsock-pci-base"
#define VHOST_USER_VSOCK_PCI(obj) \
        OBJECT_CHECK(VHostUserVSockPCI, (obj), TYPE_VHOST_USER_VSOCK_PCI)

struct VHostUserVSockPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserVSock vdev;
};

/* vhost-user-vsock-pci */

static Property vhost_user_vsock_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_vsock_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserVSockPCI *dev = VHOST_USER_VSOCK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_user_vsock_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_vsock_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vhost_user_vsock_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VSOCK;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_user_vsock_pci_instance_init(Object *obj)
{
    VHostUserVSockPCI *dev = VHOST_USER_VSOCK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_VSOCK);
}

static const VirtioPCIDeviceTypeInfo vhost_user_vsock_pci_info = {
    .base_name             = TYPE_VHOST_USER_VSOCK_PCI,
    .generic_name          = "vhost-user-vsock-pci",
    .transitional_name     = "vhost-user-vsock-pci-transitional",
    .non_transitional_name = "vhost-user-vsock-pci-non-transitional",
    .instance_size = sizeof(VHostUserVSockPCI),
    .instance_init = vhost_user_vsock_pci_instance_init,
    .class_init    = vhost_user_vsock_pci_class_init,
};

static void virtio_pci_vhost_register(void)
{
    virtio_pci_types_register(&vhost_user_vsock_pci_info);
}

type_init(virtio_pci_vhost_register)
