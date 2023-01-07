/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 msi interrupt controller.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"

static uint64_t loongarch_msi_mem_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void loongarch_msi_mem_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    LoongArchPCHMSI *s = (LoongArchPCHMSI *)opaque;
    int irq_num;

    /*
     * vector number is irq number from upper extioi intc
     * need subtract irq base to get msi vector offset
     */
    irq_num = (val & 0xff) - s->irq_base;
    trace_loongarch_msi_set_irq(irq_num);
    assert(irq_num < s->irq_num);
    qemu_set_irq(s->pch_msi_irq[irq_num], 1);
}

static const MemoryRegionOps loongarch_pch_msi_ops = {
    .read  = loongarch_msi_mem_read,
    .write = loongarch_msi_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pch_msi_irq_handler(void *opaque, int irq, int level)
{
    LoongArchPCHMSI *s = LOONGARCH_PCH_MSI(opaque);

    qemu_set_irq(s->pch_msi_irq[irq], level);
}

static void loongarch_pch_msi_realize(DeviceState *dev, Error **errp)
{
    LoongArchPCHMSI *s = LOONGARCH_PCH_MSI(dev);

    if (!s->irq_num || s->irq_num  > PCH_MSI_IRQ_NUM) {
        error_setg(errp, "Invalid 'msi_irq_num'");
        return;
    }

    s->pch_msi_irq = g_new(qemu_irq, s->irq_num);

    qdev_init_gpio_out(dev, s->pch_msi_irq, s->irq_num);
    qdev_init_gpio_in(dev, pch_msi_irq_handler, s->irq_num);
}

static void loongarch_pch_msi_unrealize(DeviceState *dev)
{
    LoongArchPCHMSI *s = LOONGARCH_PCH_MSI(dev);

    g_free(s->pch_msi_irq);
}

static void loongarch_pch_msi_init(Object *obj)
{
    LoongArchPCHMSI *s = LOONGARCH_PCH_MSI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->msi_mmio, obj, &loongarch_pch_msi_ops,
                          s, TYPE_LOONGARCH_PCH_MSI, 0x8);
    sysbus_init_mmio(sbd, &s->msi_mmio);
    msi_nonbroken = true;

}

static Property loongarch_msi_properties[] = {
    DEFINE_PROP_UINT32("msi_irq_base", LoongArchPCHMSI, irq_base, 0),
    DEFINE_PROP_UINT32("msi_irq_num",  LoongArchPCHMSI, irq_num, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void loongarch_pch_msi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = loongarch_pch_msi_realize;
    dc->unrealize = loongarch_pch_msi_unrealize;
    device_class_set_props(dc, loongarch_msi_properties);
}

static const TypeInfo loongarch_pch_msi_info = {
    .name          = TYPE_LOONGARCH_PCH_MSI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchPCHMSI),
    .instance_init = loongarch_pch_msi_init,
    .class_init    = loongarch_pch_msi_class_init,
};

static void loongarch_pch_msi_register_types(void)
{
    type_register_static(&loongarch_pch_msi_info);
}

type_init(loongarch_pch_msi_register_types)
