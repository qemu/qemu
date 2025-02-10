/*
 * Virtio scsi PCI Bindings
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paul Brook        <paul@codesourcery.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-scsi.h"
#include "qemu/module.h"
#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

typedef struct VirtIOSCSIPCI VirtIOSCSIPCI;

/*
 * virtio-scsi-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SCSI_PCI "virtio-scsi-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOSCSIPCI, VIRTIO_SCSI_PCI,
                         TYPE_VIRTIO_SCSI_PCI)

struct VirtIOSCSIPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSCSI vdev;
};

static const Property virtio_scsi_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
};

static void virtio_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSCSIPCI *dev = VIRTIO_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *proxy = DEVICE(vpci_dev);
    VirtIOSCSIConf *conf = &dev->vdev.parent_obj.conf;
    char *bus_name;

    if (conf->num_queues == VIRTIO_SCSI_AUTO_NUM_QUEUES) {
        conf->num_queues =
            virtio_pci_optimal_num_queues(VIRTIO_SCSI_VQ_NUM_FIXED);
    }

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = conf->num_queues + VIRTIO_SCSI_VQ_NUM_FIXED + 1;
    }

    /*
     * For command line compatibility, this sets the virtio-scsi-device bus
     * name as before.
     */
    if (proxy->id) {
        bus_name = g_strdup_printf("%s.0", proxy->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_scsi_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    device_class_set_props(dc, virtio_scsi_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void virtio_scsi_pci_instance_init(Object *obj)
{
    VirtIOSCSIPCI *dev = VIRTIO_SCSI_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SCSI);
}

static const VirtioPCIDeviceTypeInfo virtio_scsi_pci_info = {
    .base_name              = TYPE_VIRTIO_SCSI_PCI,
    .generic_name           = "virtio-scsi-pci",
    .transitional_name      = "virtio-scsi-pci-transitional",
    .non_transitional_name  = "virtio-scsi-pci-non-transitional",
    .instance_size = sizeof(VirtIOSCSIPCI),
    .instance_init = virtio_scsi_pci_instance_init,
    .class_init    = virtio_scsi_pci_class_init,
};

static void virtio_scsi_pci_register(void)
{
    virtio_pci_types_register(&virtio_scsi_pci_info);
}

type_init(virtio_scsi_pci_register)
