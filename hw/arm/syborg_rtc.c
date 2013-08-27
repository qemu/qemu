/*
 * Syborg RTC
 *
 * Copyright (c) 2008 CodeSourcery
 * Copyright (c) 2010, 2013 Stefan Weil
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "syborg.h"

enum {
    RTC_ID        = 0,
    RTC_LATCH     = 1,
    RTC_DATA_LOW  = 2,
    RTC_DATA_HIGH = 3
};

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    int64_t offset;
    int64_t data;
    qemu_irq irq;
} SyborgRTCState;

static uint64_t syborg_rtc_read(void *opaque, hwaddr offset,
                                unsigned size)
{
    SyborgRTCState *s = (SyborgRTCState *)opaque;
    offset &= 0xfff;
    switch (offset >> 2) {
    case RTC_ID:
        return SYBORG_ID_RTC;
    case RTC_DATA_LOW:
        return (uint32_t)s->data;
    case RTC_DATA_HIGH:
        return (uint32_t)(s->data >> 32);
    default:
        cpu_abort(cpu_single_env, "syborg_rtc_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_rtc_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    SyborgRTCState *s = (SyborgRTCState *)opaque;
    uint64_t now;

    offset &= 0xfff;
    switch (offset >> 2) {
    case RTC_LATCH:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (value >= 4) {
            s->offset = s->data - now;
        } else {
            s->data = now + s->offset;
            while (value) {
                s->data /= 1000;
                value--;
            }
        }
        break;
    case RTC_DATA_LOW:
        s->data = (s->data & ~(uint64_t)0xffffffffu) | value;
        break;
    case RTC_DATA_HIGH:
        s->data = (s->data & 0xffffffffu) | ((uint64_t)value << 32);
        break;
    default:
        cpu_abort(cpu_single_env, "syborg_rtc_write: Bad offset %x\n",
                  (int)offset);
        break;
    }
}

static const MemoryRegionOps syborg_rtc_ops = {
    .read = syborg_rtc_read,
    .write = syborg_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_syborg_rtc = {
    .name = "syborg_keyboard",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_INT64(offset, SyborgRTCState),
        VMSTATE_INT64(data, SyborgRTCState),
        VMSTATE_END_OF_LIST()
    }
};

static int syborg_rtc_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    SyborgRTCState *s = SYBORG_RTC(dev);
    struct tm tm;

    memory_region_init_io(&s->iomem, &syborg_rtc_ops, s, "rtc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    qemu_get_timedate(&tm, 0);
    s->offset = (uint64_t)mktime(&tm) * 1000000000;

    vmstate_register(&dev->qdev, -1, &vmstate_syborg_rtc, s);
    return 0;
}

static void syborg_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    dc->desc = "syborg rtc";
    //~ dc->reset = syborg_rtc_reset;
    //~ dc->vmsd = &syborg_rtc_vmsd;
    k->init = syborg_rtc_init;
}

static const TypeInfo syborg_rtc_info = {
    .name = "syborg,rtc",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SyborgRTCState),
    .class_init = syborg_rtc_class_init
};

static void syborg_rtc_register_types(void)
{
    type_register_static(&syborg_rtc_info);
}

type_init(syborg_rtc_register_types)
