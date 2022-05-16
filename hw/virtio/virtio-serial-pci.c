/*
 * Virtio serial PCI Bindings
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paul Brook        <paul@codesourcery.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-serial.h"
#include "qemu/module.h"
#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

typedef struct VirtIOSerialPCI VirtIOSerialPCI;

/*
 * virtio-serial-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SERIAL_PCI "virtio-serial-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOSerialPCI, VIRTIO_SERIAL_PCI,
                         TYPE_VIRTIO_SERIAL_PCI)

struct VirtIOSerialPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOSerial vdev;
};

static void virtio_serial_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSerialPCI *dev = VIRTIO_SERIAL_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *proxy = DEVICE(vpci_dev);
    char *bus_name;

    if (vpci_dev->class_code != PCI_CLASS_COMMUNICATION_OTHER &&
        vpci_dev->class_code != PCI_CLASS_DISPLAY_OTHER && /* qemu 0.10 */
        vpci_dev->class_code != PCI_CLASS_OTHERS) {        /* qemu-kvm  */
            vpci_dev->class_code = PCI_CLASS_COMMUNICATION_OTHER;
    }

    /* backwards-compatibility with machines that were created with
       DEV_NVECTORS_UNSPECIFIED */
    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.serial.max_virtserial_ports + 1;
    }

    /*
     * For command line compatibility, this sets the virtio-serial-device bus
     * name as before.
     */
    if (proxy->id) {
        bus_name = g_strdup_printf("%s.0", proxy->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static Property virtio_serial_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_serial_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_serial_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    device_class_set_props(dc, virtio_serial_pci_properties);
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_CONSOLE;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void virtio_serial_pci_instance_init(Object *obj)
{
    VirtIOSerialPCI *dev = VIRTIO_SERIAL_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SERIAL);
}

static const VirtioPCIDeviceTypeInfo virtio_serial_pci_info = {
    .base_name             = TYPE_VIRTIO_SERIAL_PCI,
    .generic_name          = "virtio-serial-pci",
    .transitional_name     = "virtio-serial-pci-transitional",
    .non_transitional_name = "virtio-serial-pci-non-transitional",
    .instance_size = sizeof(VirtIOSerialPCI),
    .instance_init = virtio_serial_pci_instance_init,
    .class_init    = virtio_serial_pci_class_init,
};

static void virtio_serial_pci_register(void)
{
    virtio_pci_types_register(&virtio_serial_pci_info);
}

type_init(virtio_serial_pci_register)
