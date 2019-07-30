/*
 * Virtio crypto device
 *
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *    Gonglei <arei.gonglei@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/virtio/virtio-crypto.h"
#include "qapi/error.h"
#include "qemu/module.h"

typedef struct VirtIOCryptoPCI VirtIOCryptoPCI;

/*
 * virtio-crypto-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_CRYPTO_PCI "virtio-crypto-pci"
#define VIRTIO_CRYPTO_PCI(obj) \
        OBJECT_CHECK(VirtIOCryptoPCI, (obj), TYPE_VIRTIO_CRYPTO_PCI)

struct VirtIOCryptoPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOCrypto vdev;
};

static Property virtio_crypto_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_crypto_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOCryptoPCI *vcrypto = VIRTIO_CRYPTO_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vcrypto->vdev);

    if (vcrypto->vdev.conf.cryptodev == NULL) {
        error_setg(errp, "'cryptodev' parameter expects a valid object");
        return;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
    object_property_set_link(OBJECT(vcrypto),
                 OBJECT(vcrypto->vdev.conf.cryptodev), "cryptodev",
                 NULL);
}

static void virtio_crypto_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_crypto_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = virtio_crypto_pci_properties;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_crypto_initfn(Object *obj)
{
    VirtIOCryptoPCI *dev = VIRTIO_CRYPTO_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_CRYPTO);
}

static const VirtioPCIDeviceTypeInfo virtio_crypto_pci_info = {
    .generic_name  = TYPE_VIRTIO_CRYPTO_PCI,
    .instance_size = sizeof(VirtIOCryptoPCI),
    .instance_init = virtio_crypto_initfn,
    .class_init    = virtio_crypto_pci_class_init,
};

static void virtio_crypto_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_crypto_pci_info);
}
type_init(virtio_crypto_pci_register_types)
