/*
 * OSTimer device simulation in PKUnity SoC
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"

#define TYPE_PUV3_OST "puv3_ost"
OBJECT_DECLARE_SIMPLE_TYPE(PUV3OSTState, PUV3_OST)

/* puv3 ostimer implementation. */
struct PUV3OSTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    ptimer_state *ptimer;

    uint32_t reg_OSMR0;
    uint32_t reg_OSCR;
    uint32_t reg_OSSR;
    uint32_t reg_OIER;
};

static uint64_t puv3_ost_read(void *opaque, hwaddr offset,
        unsigned size)
{
    PUV3OSTState *s = opaque;
    uint32_t ret = 0;

    switch (offset) {
    case 0x10: /* Counter Register */
        ret = s->reg_OSMR0 - (uint32_t)ptimer_get_count(s->ptimer);
        break;
    case 0x14: /* Status Register */
        ret = s->reg_OSSR;
        break;
    case 0x1c: /* Interrupt Enable Register */
        ret = s->reg_OIER;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad read offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
    }
    DPRINTF("offset 0x%x, value 0x%x\n", offset, ret);
    return ret;
}

static void puv3_ost_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    PUV3OSTState *s = opaque;

    DPRINTF("offset 0x%x, value 0x%x\n", offset, value);
    switch (offset) {
    case 0x00: /* Match Register 0 */
        ptimer_transaction_begin(s->ptimer);
        s->reg_OSMR0 = value;
        if (s->reg_OSMR0 > s->reg_OSCR) {
            ptimer_set_count(s->ptimer, s->reg_OSMR0 - s->reg_OSCR);
        } else {
            ptimer_set_count(s->ptimer, s->reg_OSMR0 +
                    (0xffffffff - s->reg_OSCR));
        }
        ptimer_run(s->ptimer, 2);
        ptimer_transaction_commit(s->ptimer);
        break;
    case 0x14: /* Status Register */
        assert(value == 0);
        if (s->reg_OSSR) {
            s->reg_OSSR = value;
            qemu_irq_lower(s->irq);
        }
        break;
    case 0x1c: /* Interrupt Enable Register */
        s->reg_OIER = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad write offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps puv3_ost_ops = {
    .read = puv3_ost_read,
    .write = puv3_ost_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void puv3_ost_tick(void *opaque)
{
    PUV3OSTState *s = opaque;

    DPRINTF("ost hit when ptimer counter from 0x%x to 0x%x!\n",
            s->reg_OSCR, s->reg_OSMR0);

    s->reg_OSCR = s->reg_OSMR0;
    if (s->reg_OIER) {
        s->reg_OSSR = 1;
        qemu_irq_raise(s->irq);
    }
}

static void puv3_ost_realize(DeviceState *dev, Error **errp)
{
    PUV3OSTState *s = PUV3_OST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->reg_OIER = 0;
    s->reg_OSSR = 0;
    s->reg_OSMR0 = 0;
    s->reg_OSCR = 0;

    sysbus_init_irq(sbd, &s->irq);

    s->ptimer = ptimer_init(puv3_ost_tick, s, PTIMER_POLICY_DEFAULT);
    ptimer_transaction_begin(s->ptimer);
    ptimer_set_freq(s->ptimer, 50 * 1000 * 1000);
    ptimer_transaction_commit(s->ptimer);

    memory_region_init_io(&s->iomem, OBJECT(s), &puv3_ost_ops, s, "puv3_ost",
            PUV3_REGS_OFFSET);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void puv3_ost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = puv3_ost_realize;
}

static const TypeInfo puv3_ost_info = {
    .name = TYPE_PUV3_OST,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PUV3OSTState),
    .class_init = puv3_ost_class_init,
};

static void puv3_ost_register_type(void)
{
    type_register_static(&puv3_ost_info);
}

type_init(puv3_ost_register_type)
