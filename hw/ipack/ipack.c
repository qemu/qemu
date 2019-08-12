/*
 * QEMU IndustryPack emulation
 *
 * Copyright (C) 2012 Igalia, S.L.
 * Author: Alberto Garcia <berto@igalia.com>
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any
 * later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/ipack/ipack.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

IPackDevice *ipack_device_find(IPackBus *bus, int32_t slot)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &BUS(bus)->children, sibling) {
        DeviceState *qdev = kid->child;
        IPackDevice *ip = IPACK_DEVICE(qdev);
        if (ip->slot == slot) {
            return ip;
        }
    }
    return NULL;
}

void ipack_bus_new_inplace(IPackBus *bus, size_t bus_size,
                           DeviceState *parent,
                           const char *name, uint8_t n_slots,
                           qemu_irq_handler handler)
{
    qbus_create_inplace(bus, bus_size, TYPE_IPACK_BUS, parent, name);
    bus->n_slots = n_slots;
    bus->set_irq = handler;
}

static void ipack_device_realize(DeviceState *dev, Error **errp)
{
    IPackDevice *idev = IPACK_DEVICE(dev);
    IPackBus *bus = IPACK_BUS(qdev_get_parent_bus(dev));
    IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(dev);

    if (idev->slot < 0) {
        idev->slot = bus->free_slot;
    }
    if (idev->slot >= bus->n_slots) {
        error_setg(errp, "Only %" PRIu8 " slots available.", bus->n_slots);
        return;
    }
    bus->free_slot = idev->slot + 1;

    idev->irq = qemu_allocate_irqs(bus->set_irq, idev, 2);

    k->realize(dev, errp);
}

static void ipack_device_unrealize(DeviceState *dev, Error **errp)
{
    IPackDevice *idev = IPACK_DEVICE(dev);
    IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    if (k->unrealize) {
        k->unrealize(dev, &err);
        error_propagate(errp, err);
        return;
    }

    qemu_free_irqs(idev->irq, 2);
}

static Property ipack_device_props[] = {
    DEFINE_PROP_INT32("slot", IPackDevice, slot, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void ipack_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_INPUT, k->categories);
    k->bus_type = TYPE_IPACK_BUS;
    k->realize = ipack_device_realize;
    k->unrealize = ipack_device_unrealize;
    k->props = ipack_device_props;
}

const VMStateDescription vmstate_ipack_device = {
    .name = "ipack_device",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(slot, IPackDevice),
        VMSTATE_END_OF_LIST()
    }
};

static const TypeInfo ipack_device_info = {
    .name          = TYPE_IPACK_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(IPackDevice),
    .class_size    = sizeof(IPackDeviceClass),
    .class_init    = ipack_device_class_init,
    .abstract      = true,
};

static const TypeInfo ipack_bus_info = {
    .name = TYPE_IPACK_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(IPackBus),
};

static void ipack_register_types(void)
{
    type_register_static(&ipack_device_info);
    type_register_static(&ipack_bus_info);
}

type_init(ipack_register_types)
