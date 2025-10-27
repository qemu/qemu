/*
 * VIRTIO Sound Device PCI Bindings
 *
 * Copyright (c) 2023 Emmanouil Pitsidianakis <manos.pitsidianakis@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "hw/audio/model.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/audio/virtio-snd.h"

/*
 * virtio-snd-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_SND_PCI "virtio-sound-pci"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOSoundPCI, VIRTIO_SND_PCI)

struct VirtIOSoundPCI {
    VirtIOPCIProxy parent_obj;

    VirtIOSound vdev;
};

static const Property virtio_snd_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
};

static void virtio_snd_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOSoundPCI *dev = VIRTIO_SND_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_pci_force_virtio_1(vpci_dev);
    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void virtio_snd_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidevklass = PCI_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_snd_pci_properties);
    dc->desc = "Virtio Sound";
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    vpciklass->realize = virtio_snd_pci_realize;
    pcidevklass->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
}

static void virtio_snd_pci_instance_init(Object *obj)
{
    VirtIOSoundPCI *dev = VIRTIO_SND_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SND);
}

static const VirtioPCIDeviceTypeInfo virtio_snd_pci_info = {
    .generic_name  = TYPE_VIRTIO_SND_PCI,
    .instance_size = sizeof(VirtIOSoundPCI),
    .instance_init = virtio_snd_pci_instance_init,
    .class_init    = virtio_snd_pci_class_init,
};

static void virtio_snd_pci_register(void)
{
    virtio_pci_types_register(&virtio_snd_pci_info);
    audio_register_model("virtio", "Virtio Sound", TYPE_VIRTIO_SND_PCI);
}

type_init(virtio_snd_pci_register);
