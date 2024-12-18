/*
 * Vhost vsock PCI Bindings
 *
 * Copyright 2015 Red Hat, Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/virtio/virtio-pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/vhost-vsock.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VHostVSockPCI VHostVSockPCI;

/*
 * vhost-vsock-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VHOST_VSOCK_PCI "vhost-vsock-pci-base"
DECLARE_INSTANCE_CHECKER(VHostVSockPCI, VHOST_VSOCK_PCI,
                         TYPE_VHOST_VSOCK_PCI)

struct VHostVSockPCI {
    VirtIOPCIProxy parent_obj;
    VHostVSock vdev;
};

/* vhost-vsock-pci */

static const Property vhost_vsock_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
};

static void vhost_vsock_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostVSockPCI *dev = VHOST_VSOCK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIODevice *virtio_dev = VIRTIO_DEVICE(vdev);

    /*
     * To avoid migration issues, we force virtio version 1 only when
     * legacy check is enabled in the new machine types (>= 5.1).
     */
    if (!virtio_legacy_check_disabled(virtio_dev)) {
        virtio_pci_force_virtio_1(vpci_dev);
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_vsock_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_vsock_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, vhost_vsock_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VSOCK;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_vsock_pci_instance_init(Object *obj)
{
    VHostVSockPCI *dev = VHOST_VSOCK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VSOCK);
}

static const VirtioPCIDeviceTypeInfo vhost_vsock_pci_info = {
    .base_name             = TYPE_VHOST_VSOCK_PCI,
    .generic_name          = "vhost-vsock-pci",
    .non_transitional_name = "vhost-vsock-pci-non-transitional",
    .instance_size = sizeof(VHostVSockPCI),
    .instance_init = vhost_vsock_pci_instance_init,
    .class_init    = vhost_vsock_pci_class_init,
};

static void virtio_pci_vhost_register(void)
{
    virtio_pci_types_register(&vhost_vsock_pci_info);
}

type_init(virtio_pci_vhost_register)
