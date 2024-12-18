/*
 * ARM11MPCore Snoop Control Unit (SCU) emulation
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 * Written by Paul Brook and Andreas Färber
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/misc/arm11scu.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"

static uint64_t mpcore_scu_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    ARM11SCUState *s = (ARM11SCUState *)opaque;
    int id;
    /* SCU */
    switch (offset) {
    case 0x00: /* Control.  */
        return s->control;
    case 0x04: /* Configuration.  */
        id = ((1 << s->num_cpu) - 1) << 4;
        return id | (s->num_cpu - 1);
    case 0x08: /* CPU status.  */
        return 0;
    case 0x0c: /* Invalidate all.  */
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpcore_priv_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void mpcore_scu_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    ARM11SCUState *s = (ARM11SCUState *)opaque;
    /* SCU */
    switch (offset) {
    case 0: /* Control register.  */
        s->control = value & 1;
        break;
    case 0x0c: /* Invalidate all.  */
        /* This is a no-op as cache is not emulated.  */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mpcore_priv_read: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps mpcore_scu_ops = {
    .read = mpcore_scu_read,
    .write = mpcore_scu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void arm11_scu_realize(DeviceState *dev, Error **errp)
{
}

static void arm11_scu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARM11SCUState *s = ARM11_SCU(obj);

    memory_region_init_io(&s->iomem, OBJECT(s),
                          &mpcore_scu_ops, s, "mpcore-scu", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const Property arm11_scu_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", ARM11SCUState, num_cpu, 1),
};

static void arm11_scu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = arm11_scu_realize;
    device_class_set_props(dc, arm11_scu_properties);
}

static const TypeInfo arm11_scu_type_info = {
    .name          = TYPE_ARM11_SCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARM11SCUState),
    .instance_init = arm11_scu_init,
    .class_init    = arm11_scu_class_init,
};

static void arm11_scu_register_types(void)
{
    type_register_static(&arm11_scu_type_info);
}

type_init(arm11_scu_register_types)
