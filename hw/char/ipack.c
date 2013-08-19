/*
 * QEMU IndustryPack emulation
 *
 * Copyright (C) 2012 Igalia, S.L.
 * Author: Alberto Garcia <agarcia@igalia.com>
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any
 * later version.
 */

#include "ipack.h"

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

void ipack_bus_new_inplace(IPackBus *bus, DeviceState *parent,
                           const char *name, uint8_t n_slots,
                           qemu_irq_handler handler)
{
    qbus_create_inplace(&bus->qbus, TYPE_IPACK_BUS, parent, name);
    bus->n_slots = n_slots;
    bus->set_irq = handler;
}

static int ipack_device_dev_init(DeviceState *qdev)
{
    IPackBus *bus = IPACK_BUS(qdev_get_parent_bus(qdev));
    IPackDevice *dev = IPACK_DEVICE(qdev);
    IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(dev);

    if (dev->slot < 0) {
        dev->slot = bus->free_slot;
    }
    if (dev->slot >= bus->n_slots) {
        return -1;
    }
    bus->free_slot = dev->slot + 1;

    dev->irq = qemu_allocate_irqs(bus->set_irq, dev, 2);

    return k->init(dev);
}

static int ipack_device_dev_exit(DeviceState *qdev)
{
    IPackDevice *dev = IPACK_DEVICE(qdev);
    IPackDeviceClass *k = IPACK_DEVICE_GET_CLASS(dev);

    if (k->exit) {
        k->exit(dev);
    }

    qemu_free_irqs(dev->irq);

    return 0;
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
    k->init = ipack_device_dev_init;
    k->exit = ipack_device_dev_exit;
    k->props = ipack_device_props;
}

const VMStateDescription vmstate_ipack_device = {
    .name = "ipack_device",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
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
