/*
 * Allwinner A10 timer device emulation
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

#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/timer/allwinner-a10-pit.h"

static uint64_t a10_pit_read(void *opaque, hwaddr offset, unsigned size)
{
    AwA10PITState *s = AW_A10_PIT(opaque);
    uint8_t index;

    switch (offset) {
    case AW_A10_PIT_TIMER_IRQ_EN:
        return s->irq_enable;
    case AW_A10_PIT_TIMER_IRQ_ST:
        return s->irq_status;
    case AW_A10_PIT_TIMER_BASE ... AW_A10_PIT_TIMER_BASE_END:
        index = offset & 0xf0;
        index >>= 4;
        index -= 1;
        switch (offset & 0x0f) {
        case AW_A10_PIT_TIMER_CONTROL:
            return s->control[index];
        case AW_A10_PIT_TIMER_INTERVAL:
            return s->interval[index];
        case AW_A10_PIT_TIMER_COUNT:
            s->count[index] = ptimer_get_count(s->timer[index]);
            return s->count[index];
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
            break;
        }
    case AW_A10_PIT_WDOG_CONTROL:
        break;
    case AW_A10_PIT_WDOG_MODE:
        break;
    case AW_A10_PIT_COUNT_LO:
        return s->count_lo;
    case AW_A10_PIT_COUNT_HI:
        return s->count_hi;
    case AW_A10_PIT_COUNT_CTL:
        return s->count_ctl;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return 0;
}

static void a10_pit_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
     AwA10PITState *s = AW_A10_PIT(opaque);
     uint8_t index;

    switch (offset) {
    case AW_A10_PIT_TIMER_IRQ_EN:
        s->irq_enable = value;
        break;
    case AW_A10_PIT_TIMER_IRQ_ST:
        s->irq_status &= ~value;
        break;
    case AW_A10_PIT_TIMER_BASE ... AW_A10_PIT_TIMER_BASE_END:
        index = offset & 0xf0;
        index >>= 4;
        index -= 1;
        switch (offset & 0x0f) {
        case AW_A10_PIT_TIMER_CONTROL:
            s->control[index] = value;
            if (s->control[index] & AW_A10_PIT_TIMER_RELOAD) {
                ptimer_set_count(s->timer[index], s->interval[index]);
            }
            if (s->control[index] & AW_A10_PIT_TIMER_EN) {
                int oneshot = 0;
                if (s->control[index] & AW_A10_PIT_TIMER_MODE) {
                    oneshot = 1;
                }
                ptimer_run(s->timer[index], oneshot);
            } else {
                ptimer_stop(s->timer[index]);
            }
            break;
        case AW_A10_PIT_TIMER_INTERVAL:
            s->interval[index] = value;
            ptimer_set_limit(s->timer[index], s->interval[index], 1);
            break;
        case AW_A10_PIT_TIMER_COUNT:
            s->count[index] = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        }
        break;
    case AW_A10_PIT_WDOG_CONTROL:
        s->watch_dog_control = value;
        break;
    case AW_A10_PIT_WDOG_MODE:
        s->watch_dog_mode = value;
        break;
    case AW_A10_PIT_COUNT_LO:
        s->count_lo = value;
        break;
    case AW_A10_PIT_COUNT_HI:
        s->count_hi = value;
        break;
    case AW_A10_PIT_COUNT_CTL:
        s->count_ctl = value;
        if (s->count_ctl & AW_A10_PIT_COUNT_RL_EN) {
            uint64_t  tmp_count = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            s->count_lo = tmp_count;
            s->count_hi = tmp_count >> 32;
            s->count_ctl &= ~AW_A10_PIT_COUNT_RL_EN;
        }
        if (s->count_ctl & AW_A10_PIT_COUNT_CLR_EN) {
            s->count_lo = 0;
            s->count_hi = 0;
            s->count_ctl &= ~AW_A10_PIT_COUNT_CLR_EN;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }
}

static const MemoryRegionOps a10_pit_ops = {
    .read = a10_pit_read,
    .write = a10_pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_a10_pit = {
    .name = "a10.pit",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_enable, AwA10PITState),
        VMSTATE_UINT32(irq_status, AwA10PITState),
        VMSTATE_UINT32_ARRAY(control, AwA10PITState, AW_A10_PIT_TIMER_NR),
        VMSTATE_UINT32_ARRAY(interval, AwA10PITState, AW_A10_PIT_TIMER_NR),
        VMSTATE_UINT32_ARRAY(count, AwA10PITState, AW_A10_PIT_TIMER_NR),
        VMSTATE_UINT32(watch_dog_mode, AwA10PITState),
        VMSTATE_UINT32(watch_dog_control, AwA10PITState),
        VMSTATE_UINT32(count_lo, AwA10PITState),
        VMSTATE_UINT32(count_hi, AwA10PITState),
        VMSTATE_UINT32(count_ctl, AwA10PITState),
        VMSTATE_PTIMER_ARRAY(timer, AwA10PITState, AW_A10_PIT_TIMER_NR),
        VMSTATE_END_OF_LIST()
    }
};

static void a10_pit_reset(DeviceState *dev)
{
    AwA10PITState *s = AW_A10_PIT(dev);
    uint8_t i;

    s->irq_enable = 0;
    s->irq_status = 0;
    for (i = 0; i < 6; i++) {
        s->control[i] = AW_A10_PIT_DEFAULT_CLOCK;
        s->interval[i] = 0;
        s->count[i] = 0;
        ptimer_stop(s->timer[i]);
    }
    s->watch_dog_mode = 0;
    s->watch_dog_control = 0;
    s->count_lo = 0;
    s->count_hi = 0;
    s->count_ctl = 0;
}

static void a10_pit_timer_cb(void *opaque)
{
    AwA10PITState *s = AW_A10_PIT(opaque);
    uint8_t i;

    for (i = 0; i < AW_A10_PIT_TIMER_NR; i++) {
        if (s->control[i] & AW_A10_PIT_TIMER_EN) {
            s->irq_status |= 1 << i;
            if (s->control[i] & AW_A10_PIT_TIMER_MODE) {
                ptimer_stop(s->timer[i]);
                s->control[i] &= ~AW_A10_PIT_TIMER_EN;
            }
            qemu_irq_pulse(s->irq[i]);
        }
    }
}

static void a10_pit_init(Object *obj)
{
    AwA10PITState *s = AW_A10_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    QEMUBH * bh[AW_A10_PIT_TIMER_NR];
    uint8_t i;

    for (i = 0; i < AW_A10_PIT_TIMER_NR; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &a10_pit_ops, s,
                          TYPE_AW_A10_PIT, 0x400);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < AW_A10_PIT_TIMER_NR; i++) {
        bh[i] = qemu_bh_new(a10_pit_timer_cb, s);
        s->timer[i] = ptimer_init(bh[i]);
        ptimer_set_freq(s->timer[i], 240000);
    }
}

static void a10_pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = a10_pit_reset;
    dc->desc = "allwinner a10 timer";
    dc->vmsd = &vmstate_a10_pit;
}

static const TypeInfo a10_pit_info = {
    .name = TYPE_AW_A10_PIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AwA10PITState),
    .instance_init = a10_pit_init,
    .class_init = a10_pit_class_init,
};

static void a10_register_types(void)
{
    type_register_static(&a10_pit_info);
}

type_init(a10_register_types);
