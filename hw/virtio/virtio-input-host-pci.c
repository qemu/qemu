/*
 * Virtio input host PCI Bindings
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/virtio/virtio-input.h"
#include "qemu/module.h"
#include "qom/object.h"

typedef struct VirtIOInputHostPCI VirtIOInputHostPCI;

#define TYPE_VIRTIO_INPUT_HOST_PCI "virtio-input-host-pci"
DECLARE_INSTANCE_CHECKER(VirtIOInputHostPCI, VIRTIO_INPUT_HOST_PCI,
                         TYPE_VIRTIO_INPUT_HOST_PCI)

struct VirtIOInputHostPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOInputHost vdev;
};

static void virtio_host_initfn(Object *obj)
{
    VirtIOInputHostPCI *dev = VIRTIO_INPUT_HOST_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_INPUT_HOST);
}

static const VirtioPCIDeviceTypeInfo virtio_input_host_pci_info = {
    .generic_name  = TYPE_VIRTIO_INPUT_HOST_PCI,
    .parent        = TYPE_VIRTIO_INPUT_PCI,
    .instance_size = sizeof(VirtIOInputHostPCI),
    .instance_init = virtio_host_initfn,
};

static void virtio_input_host_pci_register(void)
{
    virtio_pci_types_register(&virtio_input_host_pci_info);
}

type_init(virtio_input_host_pci_register)
