/*
 * QEMU Macintosh Nubus
 *
 * Copyright (c) 2013-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/nubus/nubus.h"
#include "qapi/error.h"
#include "qemu/error-report.h"


void nubus_set_irq(NubusDevice *nd, int level)
{
    NubusBus *nubus = NUBUS_BUS(qdev_get_parent_bus(DEVICE(nd)));

    qemu_set_irq(nubus->irqs[nd->slot], level);
}

static void nubus_device_realize(DeviceState *dev, Error **errp)
{
    NubusBus *nubus = NUBUS_BUS(qdev_get_parent_bus(dev));
    NubusDevice *nd = NUBUS_DEVICE(dev);
    char *name, *path;
    hwaddr slot_offset;
    int64_t size;
    int ret;

    /* Super */
    slot_offset = nd->slot * NUBUS_SUPER_SLOT_SIZE;

    name = g_strdup_printf("nubus-super-slot-%x", nd->slot);
    memory_region_init(&nd->super_slot_mem, OBJECT(dev), name,
                       NUBUS_SUPER_SLOT_SIZE);
    memory_region_add_subregion(&nubus->super_slot_io, slot_offset,
                                &nd->super_slot_mem);
    g_free(name);

    /* Normal */
    slot_offset = nd->slot * NUBUS_SLOT_SIZE;

    name = g_strdup_printf("nubus-slot-%x", nd->slot);
    memory_region_init(&nd->slot_mem, OBJECT(dev), name, NUBUS_SLOT_SIZE);
    memory_region_add_subregion(&nubus->slot_io, slot_offset,
                                &nd->slot_mem);
    g_free(name);

    /* Declaration ROM */
    if (nd->romfile != NULL) {
        path = qemu_find_file(QEMU_FILE_TYPE_BIOS, nd->romfile);
        if (path == NULL) {
            path = g_strdup(nd->romfile);
        }

        size = get_image_size(path);
        if (size < 0) {
            error_setg(errp, "failed to find romfile \"%s\"", nd->romfile);
            g_free(path);
            return;
        } else if (size == 0) {
            error_setg(errp, "romfile \"%s\" is empty", nd->romfile);
            g_free(path);
            return;
        } else if (size > NUBUS_DECL_ROM_MAX_SIZE) {
            error_setg(errp, "romfile \"%s\" too large (maximum size 128K)",
                       nd->romfile);
            g_free(path);
            return;
        }

        name = g_strdup_printf("nubus-slot-%x-declaration-rom", nd->slot);
        memory_region_init_rom(&nd->decl_rom, OBJECT(dev), name, size,
                               &error_abort);
        ret = load_image_mr(path, &nd->decl_rom);
        g_free(path);
        g_free(name);
        if (ret < 0) {
            error_setg(errp, "could not load romfile \"%s\"", nd->romfile);
            return;
        }
        memory_region_add_subregion(&nd->slot_mem, NUBUS_SLOT_SIZE - size,
                                    &nd->decl_rom);
    }
}

static Property nubus_device_properties[] = {
    DEFINE_PROP_INT32("slot", NubusDevice, slot, -1),
    DEFINE_PROP_STRING("romfile", NubusDevice, romfile),
    DEFINE_PROP_END_OF_LIST()
};

static void nubus_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nubus_device_realize;
    dc->bus_type = TYPE_NUBUS_BUS;
    device_class_set_props(dc, nubus_device_properties);
}

static const TypeInfo nubus_device_type_info = {
    .name = TYPE_NUBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(NubusDevice),
    .class_init = nubus_device_class_init,
};

static void nubus_register_types(void)
{
    type_register_static(&nubus_device_type_info);
}

type_init(nubus_register_types)
