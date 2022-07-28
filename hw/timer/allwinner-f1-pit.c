/*
 * Allwinner F1100s/F1200s timer device emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/timer/allwinner-f1-pit.h"

static void aw_f1_pit_update_irq(AwF1PITState *s)
{
    int i;

    for (i = 0; i < AW_F1_TIMER_NR; i++) {
        qemu_set_irq(s->irq[i], !!(s->irq_status & s->irq_enable & (1 << i)));
    }
}

static uint64_t aw_f1_pit_read(void *opaque, hwaddr offset, unsigned size)
{
    AwF1PITState *s = AW_F1_PIT(opaque);
    uint8_t index;

    switch (offset) {
    case AW_F1_PIT_TMR_IRQ_EN:
        return s->irq_enable;
    case AW_F1_PIT_TMR_IRQ_STA:
        return s->irq_status;
    case AW_F1_PIT_TMR_BASE ... AW_F1_PIT_TMR_BASE_END:
        index = ((offset - AW_F1_PIT_TMR_BASE) & 0x30) >> 4;
        switch (offset & 0x0f) {
        case AW_F1_PIT_CTRL:
            return s->control[index];
        case AW_F1_PIT_INTV_VALUE:
            return s->interval[index];
        case AW_F1_PIT_CUR_VALUE:
            s->count[index] = ptimer_get_count(s->timer[index]);
            return s->count[index];
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
            break;
        }
    case AW_F1_PIT_WDOG_CTRL:
        break;
    case AW_F1_PIT_WDOG_CFG:
        break;
    case AW_F1_PIT_WDOG_MODE:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return 0;
}

/* Must be called inside a ptimer transaction block for s->timer[index] */
static void aw_f1_pit_set_freq(AwF1PITState *s, int index)
{
    uint32_t prescaler, source, source_freq;

    prescaler = 1 << extract32(s->control[index], 4, 3);
    source = extract32(s->control[index], 2, 2);
    source_freq = s->clk_freq[source];

    if (source_freq) {
        ptimer_set_freq(s->timer[index], source_freq / prescaler);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid clock source %u\n",
                      __func__, source);
    }
}

static void aw_f1_pit_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
     AwF1PITState *s = AW_F1_PIT(opaque);
     uint8_t index;

    switch (offset) {
    case AW_F1_PIT_TMR_IRQ_EN:
        s->irq_enable = value;
        aw_f1_pit_update_irq(s);
        break;
    case AW_F1_PIT_TMR_IRQ_STA:
        s->irq_status &= ~value;
        aw_f1_pit_update_irq(s);
        break;
    case AW_F1_PIT_TMR_BASE ... AW_F1_PIT_TMR_BASE_END:
        index = ((offset - AW_F1_PIT_TMR_BASE) & 0x30) >> 4;
        switch (offset & 0x0f) {
        case AW_F1_PIT_CTRL:
            s->control[index] = value;
            ptimer_transaction_begin(s->timer[index]);
            aw_f1_pit_set_freq(s, index);
            if (s->control[index] & AW_F1_PIT_TMR_RELOAD) {
                ptimer_set_count(s->timer[index], s->interval[index]);
            }
            if (s->control[index] & AW_F1_PIT_TMR_EN) {
                int oneshot = 0;
                if (s->control[index] & AW_F1_PIT_TMR_MODE) {
                    oneshot = 1;
                }
                ptimer_run(s->timer[index], oneshot);
            } else {
                ptimer_stop(s->timer[index]);
            }
            ptimer_transaction_commit(s->timer[index]);
            break;
        case AW_F1_PIT_INTV_VALUE:
            s->interval[index] = value;
            ptimer_transaction_begin(s->timer[index]);
            ptimer_set_limit(s->timer[index], s->interval[index], 1);
            ptimer_transaction_commit(s->timer[index]);
            break;
        case AW_F1_PIT_CUR_VALUE:
            s->count[index] = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        }
        break;
    case AW_F1_PIT_WDOG_CTRL:
        s->watch_dog_control = value;
        break;
    case AW_F1_PIT_WDOG_CFG:
        break;
    case AW_F1_PIT_WDOG_MODE:
        s->watch_dog_mode = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }
}

static const MemoryRegionOps aw_f1_pit_ops = {
    .read = aw_f1_pit_read,
    .write = aw_f1_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property aw_f1_pit_properties[] = {
    DEFINE_PROP_UINT32("losc-clk",   AwF1PITState, clk_freq[0], 0),
    DEFINE_PROP_UINT32("osc24m-clk", AwF1PITState, clk_freq[1], 0),
    DEFINE_PROP_UINT32("clk2",       AwF1PITState, clk_freq[2], 0),
    DEFINE_PROP_UINT32("clk3",       AwF1PITState, clk_freq[3], 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_aw_f1_pit = {
    .name = "f1.pit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_enable, AwF1PITState),
        VMSTATE_UINT32(irq_status, AwF1PITState),
        VMSTATE_UINT32_ARRAY(control, AwF1PITState, AW_F1_TIMER_NR),
        VMSTATE_UINT32_ARRAY(interval, AwF1PITState, AW_F1_TIMER_NR),
        VMSTATE_UINT32_ARRAY(count, AwF1PITState, AW_F1_TIMER_NR),
        VMSTATE_UINT32(watch_dog_mode, AwF1PITState),
        VMSTATE_UINT32(watch_dog_control, AwF1PITState),
        VMSTATE_PTIMER_ARRAY(timer, AwF1PITState, AW_F1_TIMER_NR),
        VMSTATE_END_OF_LIST()
    }
};

static void aw_f1_pit_reset(DeviceState *dev)
{
    AwF1PITState *s = AW_F1_PIT(dev);
    uint8_t i;

    s->irq_enable = 0;
    s->irq_status = 0;
    aw_f1_pit_update_irq(s);

    for (i = 0; i < AW_F1_TIMER_NR; i++) {
        s->control[i] = AW_F1_PIT_CLK_SC24M;
        s->interval[i] = 0;
        s->count[i] = 0;
        ptimer_transaction_begin(s->timer[i]);
        ptimer_stop(s->timer[i]);
        aw_f1_pit_set_freq(s, i);
        ptimer_transaction_commit(s->timer[i]);
    }
    s->watch_dog_mode = 0;
    s->watch_dog_control = 0;
}

static void aw_f1_pit_timer_cb(void *opaque)
{
    AwF1TimerContext *tc = opaque;
    AwF1PITState     *s  = tc->container;
    uint8_t            i  = tc->index;

    if (s->control[i] & AW_F1_PIT_TMR_EN) {
        s->irq_status |= 1 << i;
        if (s->control[i] & AW_F1_PIT_TMR_MODE) {
            ptimer_stop(s->timer[i]);
            s->control[i] &= ~AW_F1_PIT_TMR_EN;
        }
        aw_f1_pit_update_irq(s);
    }
}

static void aw_f1_pit_init(Object *obj)
{
    AwF1PITState *s = AW_F1_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    uint8_t i;

    for (i = 0; i < AW_F1_TIMER_NR; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &aw_f1_pit_ops, s,
                          TYPE_AW_F1_PIT, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < AW_F1_TIMER_NR; i++) {
        AwF1TimerContext *tc = &s->timer_context[i];

        tc->container = s;
        tc->index = i;
        s->timer[i] = ptimer_init(aw_f1_pit_timer_cb, tc, PTIMER_POLICY_DEFAULT);
    }
}

static void aw_f1_pit_finalize(Object *obj)
{
    AwF1PITState *s = AW_F1_PIT(obj);
    int i;

    for (i = 0; i < AW_F1_TIMER_NR; i++) {
        ptimer_free(s->timer[i]);
    }
}

static void aw_f1_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = aw_f1_pit_reset;
    device_class_set_props(dc, aw_f1_pit_properties);
    dc->desc = "allwinner f1 timer";
    dc->vmsd = &vmstate_aw_f1_pit;
}

static const TypeInfo aw_f1_pit_info = {
    .name = TYPE_AW_F1_PIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AwF1PITState),
    .instance_init = aw_f1_pit_init,
    .instance_finalize = aw_f1_pit_finalize,
    .class_init = aw_f1_pit_class_init,
};

static void aw_f1_register_types(void)
{
    type_register_static(&aw_f1_pit_info);
}

type_init(aw_f1_register_types);


