/*
 * QEMU Empty Slot
 *
 * The empty_slot device emulates known to a bus but not connected devices.
 *
 * Copyright (c) 2010 Artyom Tarasenko
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any later
 * version.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/misc/empty_slot.h"
#include "qapi/error.h"
#include "trace.h"
#include "qom/object.h"

#define TYPE_EMPTY_SLOT "empty_slot"
OBJECT_DECLARE_SIMPLE_TYPE(EmptySlot, EMPTY_SLOT)

struct EmptySlot {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    char *name;
    uint64_t size;
};

static uint64_t empty_slot_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    EmptySlot *s = EMPTY_SLOT(opaque);

    trace_empty_slot_write(addr, size << 1, 0, size, s->name);

    return 0;
}

static void empty_slot_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    EmptySlot *s = EMPTY_SLOT(opaque);

    trace_empty_slot_write(addr, size << 1, val, size, s->name);
}

static const MemoryRegionOps empty_slot_ops = {
    .read = empty_slot_read,
    .write = empty_slot_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void empty_slot_init(const char *name, hwaddr addr, uint64_t slot_size)
{
    if (slot_size > 0) {
        /* Only empty slots larger than 0 byte need handling. */
        DeviceState *dev;

        dev = qdev_new(TYPE_EMPTY_SLOT);

        qdev_prop_set_uint64(dev, "size", slot_size);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(dev), 0, addr, -10000);
    }
}

static void empty_slot_realize(DeviceState *dev, Error **errp)
{
    EmptySlot *s = EMPTY_SLOT(dev);

    if (s->name == NULL) {
        s->name = g_strdup("empty-slot");
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &empty_slot_ops, s,
                          s->name, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static Property empty_slot_properties[] = {
    DEFINE_PROP_UINT64("size", EmptySlot, size, 0),
    DEFINE_PROP_STRING("name", EmptySlot, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void empty_slot_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = empty_slot_realize;
    device_class_set_props(dc, empty_slot_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo empty_slot_info = {
    .name          = TYPE_EMPTY_SLOT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EmptySlot),
    .class_init    = empty_slot_class_init,
};

static void empty_slot_register_types(void)
{
    type_register_static(&empty_slot_info);
}

type_init(empty_slot_register_types)
