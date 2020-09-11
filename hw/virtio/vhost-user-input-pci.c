/*
 * This work is licensed under the terms of the GNU LGPL, version 2 or
 * later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-input.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "virtio-pci.h"
#include "qom/object.h"

typedef struct VHostUserInputPCI VHostUserInputPCI;

#define TYPE_VHOST_USER_INPUT_PCI "vhost-user-input-pci"

DECLARE_INSTANCE_CHECKER(VHostUserInputPCI, VHOST_USER_INPUT_PCI,
                         TYPE_VHOST_USER_INPUT_PCI)

struct VHostUserInputPCI {
    VirtIOPCIProxy parent_obj;
    VHostUserInput vhi;
};

static void vhost_user_input_pci_instance_init(Object *obj)
{
    VHostUserInputPCI *dev = VHOST_USER_INPUT_PCI(obj);

    virtio_instance_init_common(obj, &dev->vhi, sizeof(dev->vhi),
                                TYPE_VHOST_USER_INPUT);

    object_property_add_alias(obj, "chardev",
                              OBJECT(&dev->vhi), "chardev");
}

static const VirtioPCIDeviceTypeInfo vhost_user_input_pci_info = {
    .generic_name = TYPE_VHOST_USER_INPUT_PCI,
    .parent = TYPE_VIRTIO_INPUT_PCI,
    .instance_size = sizeof(VHostUserInputPCI),
    .instance_init = vhost_user_input_pci_instance_init,
};

static void vhost_user_input_pci_register(void)
{
    virtio_pci_types_register(&vhost_user_input_pci_info);
}

type_init(vhost_user_input_pci_register)
