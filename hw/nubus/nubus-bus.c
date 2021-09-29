/*
 * QEMU Macintosh Nubus
 *
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * References:
 *   Nubus Specification (TI)
 *     http://www.bitsavers.org/pdf/ti/nubus/2242825-0001_NuBus_Spec1983.pdf
 *
 *   Designing Cards and Drivers for the Macintosh Family (Apple)
 */

#include "qemu/osdep.h"
#include "hw/nubus/nubus.h"
#include "qapi/error.h"
#include "trace.h"


static NubusBus *nubus_find(void)
{
    /* Returns NULL unless there is exactly one nubus device */
    return NUBUS_BUS(object_resolve_path_type("", TYPE_NUBUS_BUS, NULL));
}

static MemTxResult nubus_slot_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size, MemTxAttrs attrs)
{
    trace_nubus_slot_write(addr, val, size);
    return MEMTX_DECODE_ERROR;
}

static MemTxResult nubus_slot_read(void *opaque, hwaddr addr, uint64_t *data,
                                   unsigned size, MemTxAttrs attrs)
{
    trace_nubus_slot_read(addr, size);
    return MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps nubus_slot_ops = {
    .read_with_attrs  = nubus_slot_read,
    .write_with_attrs = nubus_slot_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static MemTxResult nubus_super_slot_write(void *opaque, hwaddr addr,
                                          uint64_t val, unsigned size,
                                          MemTxAttrs attrs)
{
    trace_nubus_super_slot_write(addr, val, size);
    return MEMTX_DECODE_ERROR;
}

static MemTxResult nubus_super_slot_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    trace_nubus_super_slot_read(addr, size);
    return MEMTX_DECODE_ERROR;
}

static const MemoryRegionOps nubus_super_slot_ops = {
    .read_with_attrs = nubus_super_slot_read,
    .write_with_attrs = nubus_super_slot_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void nubus_unrealize(BusState *bus)
{
    NubusBus *nubus = NUBUS_BUS(bus);

    address_space_destroy(&nubus->nubus_as);
}

static void nubus_realize(BusState *bus, Error **errp)
{
    NubusBus *nubus = NUBUS_BUS(bus);

    if (!nubus_find()) {
        error_setg(errp, "at most one %s device is permitted", TYPE_NUBUS_BUS);
        return;
    }

    address_space_init(&nubus->nubus_as, &nubus->nubus_mr, "nubus");
}

static void nubus_init(Object *obj)
{
    NubusBus *nubus = NUBUS_BUS(obj);

    memory_region_init(&nubus->nubus_mr, obj, "nubus", 0x100000000);

    memory_region_init_io(&nubus->super_slot_io, obj, &nubus_super_slot_ops,
                          nubus, "nubus-super-slots",
                          (NUBUS_SUPER_SLOT_NB + 1) * NUBUS_SUPER_SLOT_SIZE);
    memory_region_add_subregion(&nubus->nubus_mr, 0x0, &nubus->super_slot_io);

    memory_region_init_io(&nubus->slot_io, obj, &nubus_slot_ops,
                          nubus, "nubus-slots",
                          NUBUS_SLOT_NB * NUBUS_SLOT_SIZE);
    memory_region_add_subregion(&nubus->nubus_mr,
                                (NUBUS_SUPER_SLOT_NB + 1) *
                                NUBUS_SUPER_SLOT_SIZE, &nubus->slot_io);

    nubus->slot_available_mask = MAKE_64BIT_MASK(NUBUS_FIRST_SLOT,
                                                 NUBUS_SLOT_NB);
}

static char *nubus_get_dev_path(DeviceState *dev)
{
    NubusDevice *nd = NUBUS_DEVICE(dev);
    BusState *bus = qdev_get_parent_bus(dev);
    char *p = qdev_get_dev_path(bus->parent);

    if (p) {
        char *ret = g_strdup_printf("%s/%s/%02x", p, bus->name, nd->slot);
        g_free(p);
        return ret;
    } else {
        return g_strdup_printf("%s/%02x", bus->name, nd->slot);
    }
}

static bool nubus_check_address(BusState *bus, DeviceState *dev, Error **errp)
{
    NubusDevice *nd = NUBUS_DEVICE(dev);
    NubusBus *nubus = NUBUS_BUS(bus);

    if (nd->slot == -1) {
        /* No slot specified, find first available free slot */
        int s = ctz32(nubus->slot_available_mask);
        if (s != 32) {
            nd->slot = s;
        } else {
            error_setg(errp, "Cannot register nubus card, no free slot "
                             "available");
            return false;
        }
    } else {
        /* Slot specified, make sure the slot is available */
        if (!(nubus->slot_available_mask & BIT(nd->slot))) {
            error_setg(errp, "Cannot register nubus card, slot %d is "
                             "unavailable or already occupied", nd->slot);
            return false;
        }
    }

    nubus->slot_available_mask &= ~BIT(nd->slot);
    return true;
}

static void nubus_class_init(ObjectClass *oc, void *data)
{
    BusClass *bc = BUS_CLASS(oc);

    bc->realize = nubus_realize;
    bc->unrealize = nubus_unrealize;
    bc->check_address = nubus_check_address;
    bc->get_dev_path = nubus_get_dev_path;
}

static const TypeInfo nubus_bus_info = {
    .name = TYPE_NUBUS_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(NubusBus),
    .instance_init = nubus_init,
    .class_init = nubus_class_init,
};

static void nubus_register_types(void)
{
    type_register_static(&nubus_bus_info);
}

type_init(nubus_register_types)
