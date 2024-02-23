/*
 * QEMU sun4v Real Time Clock device
 *
 * The sun4v_rtc device (sun4v tod clock)
 *
 * Copyright (c) 2016 Artyom Tarasenko
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any later
 * version.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/rtc/sun4v-rtc.h"
#include "trace.h"
#include "qom/object.h"


#define TYPE_SUN4V_RTC "sun4v_rtc"
OBJECT_DECLARE_SIMPLE_TYPE(Sun4vRtc, SUN4V_RTC)

struct Sun4vRtc {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

static uint64_t sun4v_rtc_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    uint64_t val = get_clock_realtime() / NANOSECONDS_PER_SECOND;
    if (!(addr & 4ULL)) {
        /* accessing the high 32 bits */
        val >>= 32;
    }
    trace_sun4v_rtc_read(addr, val);
    return val;
}

static void sun4v_rtc_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    trace_sun4v_rtc_write(addr, val);
}

static const MemoryRegionOps sun4v_rtc_ops = {
    .read = sun4v_rtc_read,
    .write = sun4v_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void sun4v_rtc_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_SUN4V_RTC);
    s = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
}

static void sun4v_rtc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Sun4vRtc *s = SUN4V_RTC(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &sun4v_rtc_ops, s,
                          "sun4v-rtc", 0x08ULL);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sun4v_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sun4v_rtc_realize;
}

static const TypeInfo sun4v_rtc_info = {
    .name          = TYPE_SUN4V_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Sun4vRtc),
    .class_init    = sun4v_rtc_class_init,
};

static void sun4v_rtc_register_types(void)
{
    type_register_static(&sun4v_rtc_info);
}

type_init(sun4v_rtc_register_types)
