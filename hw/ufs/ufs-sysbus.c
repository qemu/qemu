/*
 * QEMU Universal Flash Storage (UFS) sysbus controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-properties.h"
#include "ufs-sysbus.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"

static void ufs_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysbusUfsState *s = SYSBUS_UFS(dev);

    if (!ufs_realize(&s->ufs, dev, &address_space_memory, errp)) {
        return;
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->ufs.iomem);
}

static void ufs_sysbus_unrealize(DeviceState *dev)
{
    SysbusUfsState *s = SYSBUS_UFS(dev);

    ufs_unrealize(&s->ufs);
}

static void ufs_sysbus_init(Object *obj)
{
    SysbusUfsState *s = SYSBUS_UFS(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->ufs.irq);
}

static const Property ufs_sysbus_props[] = {
    DEFINE_PROP_STRING("serial", SysbusUfsState, ufs.params.serial),
    DEFINE_PROP_UINT8("nutrs", SysbusUfsState, ufs.params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", SysbusUfsState, ufs.params.nutmrs, 8),
    DEFINE_PROP_BOOL("mcq", SysbusUfsState, ufs.params.mcq, false),
    DEFINE_PROP_UINT8("mcq-maxq", SysbusUfsState, ufs.params.mcq_maxq, 2),
    DEFINE_PROP_UINT32("wb-max-size", SysbusUfsState, ufs.params.wb_max_size,
                       0x400),
    DEFINE_PROP_UINT32("wb-min-size", SysbusUfsState, ufs.params.wb_min_size,
                       0x100),
};

static const VMStateDescription ufs_sysbus_vmstate = {
    .name = TYPE_SYSBUS_UFS,
    .unmigratable = 1,
};

static void ufs_sysbus_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ufs_sysbus_realize;
    dc->unrealize = ufs_sysbus_unrealize;
    dc->vmsd = &ufs_sysbus_vmstate;
    dc->desc = "Universal Flash Storage";
    device_class_set_props(dc, ufs_sysbus_props);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo ufs_sysbus_info = {
    .name = TYPE_SYSBUS_UFS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysbusUfsState),
    .instance_init = ufs_sysbus_init,
    .class_init = ufs_sysbus_class_init,
};

static void ufs_sysbus_register_types(void)
{
    type_register_static(&ufs_sysbus_info);
}

type_init(ufs_sysbus_register_types)
