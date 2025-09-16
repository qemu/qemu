/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch direct interrupt controller.
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_dintc.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/qdev-properties.h"

static uint64_t loongarch_dintc_mem_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    return 0;
}

static void loongarch_dintc_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    return;
}


static const MemoryRegionOps loongarch_dintc_ops = {
    .read = loongarch_dintc_mem_read,
    .write = loongarch_dintc_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_dintc_realize(DeviceState *dev, Error **errp)
{
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_GET_CLASS(dev);

    Error *local_err = NULL;
    lac->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    return;
}

static void loongarch_dintc_unrealize(DeviceState *dev)
{
    return;
}

static void loongarch_dintc_init(Object *obj)
{
    LoongArchDINTCState *s = LOONGARCH_DINTC(obj);
    SysBusDevice *shd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->dintc_mmio, OBJECT(s), &loongarch_dintc_ops,
                          s, TYPE_LOONGARCH_DINTC, VIRT_DINTC_SIZE);
    sysbus_init_mmio(shd, &s->dintc_mmio);
    msi_nonbroken = true;
    return;
}

static void loongarch_dintc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchDINTCClass *lac = LOONGARCH_DINTC_CLASS(klass);

    dc->unrealize = loongarch_dintc_unrealize;
    device_class_set_parent_realize(dc, loongarch_dintc_realize,
                                    &lac->parent_realize);
}

static const TypeInfo loongarch_dintc_info = {
    .name          = TYPE_LOONGARCH_DINTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchDINTCState),
    .instance_init = loongarch_dintc_init,
    .class_init    = loongarch_dintc_class_init,
};

static void loongarch_dintc_register_types(void)
{
    type_register_static(&loongarch_dintc_info);
}

type_init(loongarch_dintc_register_types)
