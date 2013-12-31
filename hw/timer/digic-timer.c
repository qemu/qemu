/*
 * QEMU model of the Canon DIGIC timer block.
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This model is based on reverse engineering efforts
 * made by CHDK (http://chdk.wikia.com) and
 * Magic Lantern (http://www.magiclantern.fm) projects
 * contributors.
 *
 * See "Timer/Clock Module" docs here:
 *   http://magiclantern.wikia.com/wiki/Register_Map
 *
 * The QEMU model of the OSTimer in PKUnity SoC by Guan Xuetao
 * is used as a template.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"

#include "hw/timer/digic-timer.h"

static const VMStateDescription vmstate_digic_timer = {
    .name = "digic.timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(ptimer, DigicTimerState),
        VMSTATE_UINT32(control, DigicTimerState),
        VMSTATE_UINT32(relvalue, DigicTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void digic_timer_reset(DeviceState *dev)
{
    DigicTimerState *s = DIGIC_TIMER(dev);

    ptimer_stop(s->ptimer);
    s->control = 0;
    s->relvalue = 0;
}

static uint64_t digic_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    DigicTimerState *s = opaque;
    uint64_t ret = 0;

    switch (offset) {
    case DIGIC_TIMER_CONTROL:
        ret = s->control;
        break;
    case DIGIC_TIMER_RELVALUE:
        ret = s->relvalue;
        break;
    case DIGIC_TIMER_VALUE:
        ret = ptimer_get_count(s->ptimer) & 0xffff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "digic-timer: read access to unknown register 0x"
                      TARGET_FMT_plx, offset);
    }

    return ret;
}

static void digic_timer_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    DigicTimerState *s = opaque;

    switch (offset) {
    case DIGIC_TIMER_CONTROL:
        if (value & DIGIC_TIMER_CONTROL_RST) {
            digic_timer_reset((DeviceState *)s);
            break;
        }

        if (value & DIGIC_TIMER_CONTROL_EN) {
            ptimer_run(s->ptimer, 0);
        }

        s->control = (uint32_t)value;
        break;

    case DIGIC_TIMER_RELVALUE:
        s->relvalue = extract32(value, 0, 16);
        ptimer_set_limit(s->ptimer, s->relvalue, 1);
        break;

    case DIGIC_TIMER_VALUE:
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "digic-timer: read access to unknown register 0x"
                      TARGET_FMT_plx, offset);
    }
}

static const MemoryRegionOps digic_timer_ops = {
    .read = digic_timer_read,
    .write = digic_timer_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void digic_timer_init(Object *obj)
{
    DigicTimerState *s = DIGIC_TIMER(obj);

    s->ptimer = ptimer_init(NULL);

    /*
     * FIXME: there is no documentation on Digic timer
     * frequency setup so let it always run at 1 MHz
     */
    ptimer_set_freq(s->ptimer, 1 * 1000 * 1000);

    memory_region_init_io(&s->iomem, OBJECT(s), &digic_timer_ops, s,
                          TYPE_DIGIC_TIMER, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void digic_timer_class_init(ObjectClass *klass, void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = digic_timer_reset;
    dc->vmsd = &vmstate_digic_timer;
}

static const TypeInfo digic_timer_info = {
    .name = TYPE_DIGIC_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DigicTimerState),
    .instance_init = digic_timer_init,
    .class_init = digic_timer_class_init,
};

static void digic_timer_register_type(void)
{
    type_register_static(&digic_timer_info);
}

type_init(digic_timer_register_type)
