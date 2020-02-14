/*
 * Goldfish virtual platform RTC
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * For more details on Google Goldfish virtual platform refer:
 * https://android.googlesource.com/platform/external/qemu/+/refs/heads/emu-2.0-release/docs/GOLDFISH-VIRTUAL-HARDWARE.TXT
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/rtc/goldfish_rtc.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"

#include "trace.h"

#define RTC_TIME_LOW            0x00
#define RTC_TIME_HIGH           0x04
#define RTC_ALARM_LOW           0x08
#define RTC_ALARM_HIGH          0x0c
#define RTC_IRQ_ENABLED         0x10
#define RTC_CLEAR_ALARM         0x14
#define RTC_ALARM_STATUS        0x18
#define RTC_CLEAR_INTERRUPT     0x1c

static void goldfish_rtc_update(GoldfishRTCState *s)
{
    qemu_set_irq(s->irq, (s->irq_pending & s->irq_enabled) ? 1 : 0);
}

static void goldfish_rtc_interrupt(void *opaque)
{
    GoldfishRTCState *s = (GoldfishRTCState *)opaque;

    s->alarm_running = 0;
    s->irq_pending = 1;
    goldfish_rtc_update(s);
}

static uint64_t goldfish_rtc_get_count(GoldfishRTCState *s)
{
    return s->tick_offset + (uint64_t)qemu_clock_get_ns(rtc_clock);
}

static void goldfish_rtc_clear_alarm(GoldfishRTCState *s)
{
    timer_del(s->timer);
    s->alarm_running = 0;
}

static void goldfish_rtc_set_alarm(GoldfishRTCState *s)
{
    uint64_t ticks = goldfish_rtc_get_count(s);
    uint64_t event = s->alarm_next;

    if (event <= ticks) {
        goldfish_rtc_clear_alarm(s);
        goldfish_rtc_interrupt(s);
    } else {
        /*
         * We should be setting timer expiry to:
         *     qemu_clock_get_ns(rtc_clock) + (event - ticks)
         * but this is equivalent to:
         *     event - s->tick_offset
         */
        timer_mod(s->timer, event - s->tick_offset);
        s->alarm_running = 1;
    }
}

static uint64_t goldfish_rtc_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    GoldfishRTCState *s = opaque;
    uint64_t r = 0;

    switch (offset) {
    case RTC_TIME_LOW:
        r = goldfish_rtc_get_count(s) & 0xffffffff;
        break;
    case RTC_TIME_HIGH:
        r = goldfish_rtc_get_count(s) >> 32;
        break;
    case RTC_ALARM_LOW:
        r = s->alarm_next & 0xffffffff;
        break;
    case RTC_ALARM_HIGH:
        r = s->alarm_next >> 32;
        break;
    case RTC_IRQ_ENABLED:
        r = s->irq_enabled;
        break;
    case RTC_ALARM_STATUS:
        r = s->alarm_running;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: offset 0x%x is UNIMP.\n", __func__, (uint32_t)offset);
        break;
    }

    trace_goldfish_rtc_read(offset, r);

    return r;
}

static void goldfish_rtc_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    GoldfishRTCState *s = opaque;
    uint64_t current_tick, new_tick;

    switch (offset) {
    case RTC_TIME_LOW:
        current_tick = goldfish_rtc_get_count(s);
        new_tick = deposit64(current_tick, 0, 32, value);
        s->tick_offset += new_tick - current_tick;
        break;
    case RTC_TIME_HIGH:
        current_tick = goldfish_rtc_get_count(s);
        new_tick = deposit64(current_tick, 32, 32, value);
        s->tick_offset += new_tick - current_tick;
        break;
    case RTC_ALARM_LOW:
        s->alarm_next = deposit64(s->alarm_next, 0, 32, value);
        goldfish_rtc_set_alarm(s);
        break;
    case RTC_ALARM_HIGH:
        s->alarm_next = deposit64(s->alarm_next, 32, 32, value);
        break;
    case RTC_IRQ_ENABLED:
        s->irq_enabled = (uint32_t)(value & 0x1);
        goldfish_rtc_update(s);
        break;
    case RTC_CLEAR_ALARM:
        goldfish_rtc_clear_alarm(s);
        break;
    case RTC_CLEAR_INTERRUPT:
        s->irq_pending = 0;
        goldfish_rtc_update(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: offset 0x%x is UNIMP.\n", __func__, (uint32_t)offset);
        break;
    }

    trace_goldfish_rtc_write(offset, value);
}

static int goldfish_rtc_pre_save(void *opaque)
{
    uint64_t delta;
    GoldfishRTCState *s = opaque;

    /*
     * We want to migrate this offset, which sounds straightforward.
     * Unfortunately, we cannot directly pass tick_offset because
     * rtc_clock on destination Host might not be same source Host.
     *
     * To tackle, this we pass tick_offset relative to vm_clock from
     * source Host and make it relative to rtc_clock at destination Host.
     */
    delta = qemu_clock_get_ns(rtc_clock) -
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tick_offset_vmstate = s->tick_offset + delta;

    return 0;
}

static int goldfish_rtc_post_load(void *opaque, int version_id)
{
    uint64_t delta;
    GoldfishRTCState *s = opaque;

    /*
     * We extract tick_offset from tick_offset_vmstate by doing
     * reverse math compared to pre_save() function.
     */
    delta = qemu_clock_get_ns(rtc_clock) -
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->tick_offset = s->tick_offset_vmstate - delta;

    return 0;
}

static const MemoryRegionOps goldfish_rtc_ops = {
    .read = goldfish_rtc_read,
    .write = goldfish_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static const VMStateDescription goldfish_rtc_vmstate = {
    .name = TYPE_GOLDFISH_RTC,
    .version_id = 1,
    .pre_save = goldfish_rtc_pre_save,
    .post_load = goldfish_rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(tick_offset_vmstate, GoldfishRTCState),
        VMSTATE_UINT64(alarm_next, GoldfishRTCState),
        VMSTATE_UINT32(alarm_running, GoldfishRTCState),
        VMSTATE_UINT32(irq_pending, GoldfishRTCState),
        VMSTATE_UINT32(irq_enabled, GoldfishRTCState),
        VMSTATE_END_OF_LIST()
    }
};

static void goldfish_rtc_reset(DeviceState *dev)
{
    GoldfishRTCState *s = GOLDFISH_RTC(dev);
    struct tm tm;

    timer_del(s->timer);

    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm);
    s->tick_offset *= NANOSECONDS_PER_SECOND;
    s->tick_offset -= qemu_clock_get_ns(rtc_clock);
    s->tick_offset_vmstate = 0;
    s->alarm_next = 0;
    s->alarm_running = 0;
    s->irq_pending = 0;
    s->irq_enabled = 0;
}

static void goldfish_rtc_realize(DeviceState *d, Error **errp)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    GoldfishRTCState *s = GOLDFISH_RTC(d);

    memory_region_init_io(&s->iomem, OBJECT(s), &goldfish_rtc_ops, s,
                          "goldfish_rtc", 0x24);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);

    s->timer = timer_new_ns(rtc_clock, goldfish_rtc_interrupt, s);
}

static void goldfish_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = goldfish_rtc_realize;
    dc->reset = goldfish_rtc_reset;
    dc->vmsd = &goldfish_rtc_vmstate;
}

static const TypeInfo goldfish_rtc_info = {
    .name          = TYPE_GOLDFISH_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GoldfishRTCState),
    .class_init    = goldfish_rtc_class_init,
};

static void goldfish_rtc_register_types(void)
{
    type_register_static(&goldfish_rtc_info);
}

type_init(goldfish_rtc_register_types)
