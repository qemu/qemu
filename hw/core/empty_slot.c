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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "hw/empty_slot.h"

//#define DEBUG_EMPTY_SLOT

#ifdef DEBUG_EMPTY_SLOT
#define DPRINTF(fmt, ...)                                       \
    do { printf("empty_slot: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#define TYPE_EMPTY_SLOT "empty_slot"
#define EMPTY_SLOT(obj) OBJECT_CHECK(EmptySlot, (obj), TYPE_EMPTY_SLOT)

typedef struct EmptySlot {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint64_t size;
} EmptySlot;

static uint64_t empty_slot_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    DPRINTF("read from " TARGET_FMT_plx "\n", addr);
    return 0;
}

static void empty_slot_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    DPRINTF("write 0x%x to " TARGET_FMT_plx "\n", (unsigned)val, addr);
}

static const MemoryRegionOps empty_slot_ops = {
    .read = empty_slot_read,
    .write = empty_slot_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void empty_slot_init(hwaddr addr, uint64_t slot_size)
{
    if (slot_size > 0) {
        /* Only empty slots larger than 0 byte need handling. */
        DeviceState *dev;
        SysBusDevice *s;
        EmptySlot *e;

        dev = qdev_create(NULL, TYPE_EMPTY_SLOT);
        s = SYS_BUS_DEVICE(dev);
        e = EMPTY_SLOT(dev);
        e->size = slot_size;

        qdev_init_nofail(dev);

        sysbus_mmio_map(s, 0, addr);
    }
}

static void empty_slot_realize(DeviceState *dev, Error **errp)
{
    EmptySlot *s = EMPTY_SLOT(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &empty_slot_ops, s,
                          "empty-slot", s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void empty_slot_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = empty_slot_realize;
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
