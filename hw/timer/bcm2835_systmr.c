/*
 * BCM2835 SYS timer emulation
 *
 * Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Datasheet: BCM2835 ARM Peripherals (C6357-M-1398)
 * https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf
 *
 * Only the free running 64-bit counter is implemented.
 * The 4 COMPARE registers and the interruption are not implemented.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/timer/bcm2835_systmr.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"
#include "trace.h"

REG32(CTRL_STATUS,  0x00)
REG32(COUNTER_LOW,  0x04)
REG32(COUNTER_HIGH, 0x08)
REG32(COMPARE0,     0x0c)
REG32(COMPARE1,     0x10)
REG32(COMPARE2,     0x14)
REG32(COMPARE3,     0x18)

static void bcm2835_systmr_timer_expire(void *opaque)
{
    BCM2835SystemTimerCompare *tmr = opaque;

    trace_bcm2835_systmr_timer_expired(tmr->id);
    tmr->state->reg.ctrl_status |= 1 << tmr->id;
    qemu_set_irq(tmr->irq, 1);
}

static uint64_t bcm2835_systmr_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    BCM2835SystemTimerState *s = BCM2835_SYSTIMER(opaque);
    uint64_t r = 0;

    switch (offset) {
    case A_CTRL_STATUS:
        r = s->reg.ctrl_status;
        break;
    case A_COMPARE0 ... A_COMPARE3:
        r = s->reg.compare[(offset - A_COMPARE0) >> 2];
        break;
    case A_COUNTER_LOW:
    case A_COUNTER_HIGH:
        /* Free running counter at 1MHz */
        r = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
        r >>= 8 * (offset - A_COUNTER_LOW);
        r &= UINT32_MAX;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
    trace_bcm2835_systmr_read(offset, r);

    return r;
}

static void bcm2835_systmr_write(void *opaque, hwaddr offset,
                                 uint64_t value64, unsigned size)
{
    BCM2835SystemTimerState *s = BCM2835_SYSTIMER(opaque);
    int index;
    uint32_t value = value64;
    uint32_t triggers_delay_us;
    uint64_t now;

    trace_bcm2835_systmr_write(offset, value);
    switch (offset) {
    case A_CTRL_STATUS:
        s->reg.ctrl_status &= ~value; /* Ack */
        for (index = 0; index < ARRAY_SIZE(s->tmr); index++) {
            if (extract32(value, index, 1)) {
                trace_bcm2835_systmr_irq_ack(index);
                qemu_set_irq(s->tmr[index].irq, 0);
            }
        }
        break;
    case A_COMPARE0 ... A_COMPARE3:
        index = (offset - A_COMPARE0) >> 2;
        s->reg.compare[index] = value;
        now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
        /* Compare lower 32-bits of the free-running counter. */
        triggers_delay_us = value - now;
        trace_bcm2835_systmr_run(index, triggers_delay_us);
        timer_mod(&s->tmr[index].timer, now + triggers_delay_us);
        break;
    case A_COUNTER_LOW:
    case A_COUNTER_HIGH:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read-only ofs 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps bcm2835_systmr_ops = {
    .read = bcm2835_systmr_read,
    .write = bcm2835_systmr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void bcm2835_systmr_reset(DeviceState *dev)
{
    BCM2835SystemTimerState *s = BCM2835_SYSTIMER(dev);

    memset(&s->reg, 0, sizeof(s->reg));
}

static void bcm2835_systmr_realize(DeviceState *dev, Error **errp)
{
    BCM2835SystemTimerState *s = BCM2835_SYSTIMER(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &bcm2835_systmr_ops,
                          s, "bcm2835-sys-timer", 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    for (size_t i = 0; i < ARRAY_SIZE(s->tmr); i++) {
        s->tmr[i].id = i;
        s->tmr[i].state = s;
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->tmr[i].irq);
        timer_init_us(&s->tmr[i].timer, QEMU_CLOCK_VIRTUAL,
                      bcm2835_systmr_timer_expire, &s->tmr[i]);
    }
}

static const VMStateDescription bcm2835_systmr_vmstate = {
    .name = "bcm2835_sys_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(reg.ctrl_status, BCM2835SystemTimerState),
        VMSTATE_UINT32_ARRAY(reg.compare, BCM2835SystemTimerState,
                             BCM2835_SYSTIMER_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_systmr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_systmr_realize;
    device_class_set_legacy_reset(dc, bcm2835_systmr_reset);
    dc->vmsd = &bcm2835_systmr_vmstate;
}

static const TypeInfo bcm2835_systmr_info = {
    .name = TYPE_BCM2835_SYSTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835SystemTimerState),
    .class_init = bcm2835_systmr_class_init,
};

static void bcm2835_systmr_register_types(void)
{
    type_register_static(&bcm2835_systmr_info);
}

type_init(bcm2835_systmr_register_types);
