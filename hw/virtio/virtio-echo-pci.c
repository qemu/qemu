#include "qemu/osdep.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio.h"

#define TYPE_VIRTIO_ECHO2_PCI "virtio-echo2-pci"
typedef struct VirtIOEcho2PCI { VirtIOPCIProxy parent_obj; } VirtIOEcho2PCI;

static void virtio_echo2_pci_realize(VirtIOPCIProxy *vpci, Error **errp)
{
    DeviceState *vdev = qdev_new("virtio-echo2");  // your core device type
    qdev_realize_and_unref(vdev, BUS(&vpci->bus), errp);
    if (*errp) { return; }
    virtio_pci_realize(vpci, vdev, errp);
}

static void virtio_echo2_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIDeviceClass *k = VIRTIO_PCI_DEVICE_CLASS(klass);
    dc->desc = "Virtio Echo2 PCI";
    k->realize = virtio_echo2_pci_realize;
}

static const TypeInfo virtio_echo2_pci_info = {
    .name          = TYPE_VIRTIO_ECHO2_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOEcho2PCI),
    .class_init    = virtio_echo2_pci_class_init,
};

static void virtio_echo2_pci_register_types(void)
{
    type_register_static(&virtio_echo2_pci_info);
}
type_init(virtio_echo2_pci_register_types);
