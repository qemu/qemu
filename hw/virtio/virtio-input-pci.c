/*
 * Virtio input PCI Bindings
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-input.h"
#include "qemu/module.h"

typedef struct VirtIOInputPCI VirtIOInputPCI;
typedef struct VirtIOInputHIDPCI VirtIOInputHIDPCI;

/*
 * virtio-input-pci: This extends VirtioPCIProxy.
 */
#define VIRTIO_INPUT_PCI(obj) \
        OBJECT_CHECK(VirtIOInputPCI, (obj), TYPE_VIRTIO_INPUT_PCI)

struct VirtIOInputPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOInput vdev;
};

#define TYPE_VIRTIO_INPUT_HID_PCI "virtio-input-hid-pci"
#define TYPE_VIRTIO_KEYBOARD_PCI  "virtio-keyboard-pci"
#define TYPE_VIRTIO_MOUSE_PCI     "virtio-mouse-pci"
#define TYPE_VIRTIO_TABLET_PCI    "virtio-tablet-pci"
#define VIRTIO_INPUT_HID_PCI(obj) \
        OBJECT_CHECK(VirtIOInputHIDPCI, (obj), TYPE_VIRTIO_INPUT_HID_PCI)

struct VirtIOInputHIDPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOInputHID vdev;
};

static Property virtio_input_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_input_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOInputPCI *vinput = VIRTIO_INPUT_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vinput->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_input_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_input_pci_properties);
    k->realize = virtio_input_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    pcidev_k->class_id = PCI_CLASS_INPUT_OTHER;
}

static void virtio_input_hid_kbd_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    pcidev_k->class_id = PCI_CLASS_INPUT_KEYBOARD;
}

static void virtio_input_hid_mouse_pci_class_init(ObjectClass *klass,
                                                  void *data)
{
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    pcidev_k->class_id = PCI_CLASS_INPUT_MOUSE;
}

static void virtio_keyboard_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_KEYBOARD);
}

static void virtio_mouse_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_MOUSE);
}

static void virtio_tablet_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_TABLET);
}

static const TypeInfo virtio_input_pci_info = {
    .name          = TYPE_VIRTIO_INPUT_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOInputPCI),
    .class_init    = virtio_input_pci_class_init,
    .abstract      = true,
};

static const TypeInfo virtio_input_hid_pci_info = {
    .name          = TYPE_VIRTIO_INPUT_HID_PCI,
    .parent        = TYPE_VIRTIO_INPUT_PCI,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .abstract      = true,
};

static const VirtioPCIDeviceTypeInfo virtio_keyboard_pci_info = {
    .generic_name  = TYPE_VIRTIO_KEYBOARD_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .class_init    = virtio_input_hid_kbd_pci_class_init,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_keyboard_initfn,
};

static const VirtioPCIDeviceTypeInfo virtio_mouse_pci_info = {
    .generic_name  = TYPE_VIRTIO_MOUSE_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .class_init    = virtio_input_hid_mouse_pci_class_init,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_mouse_initfn,
};

static const VirtioPCIDeviceTypeInfo virtio_tablet_pci_info = {
    .generic_name  = TYPE_VIRTIO_TABLET_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_tablet_initfn,
};

static void virtio_pci_input_register(void)
{
    /* Base types: */
    type_register_static(&virtio_input_pci_info);
    type_register_static(&virtio_input_hid_pci_info);

    /* Implementations: */
    virtio_pci_types_register(&virtio_keyboard_pci_info);
    virtio_pci_types_register(&virtio_mouse_pci_info);
    virtio_pci_types_register(&virtio_tablet_pci_info);
}

type_init(virtio_pci_input_register)
