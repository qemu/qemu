/*
 * Syborg RTC
 *
 * Copyright (c) 2008 CodeSourcery
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

#include "sysbus.h"
#include "qemu-timer.h"
#include "syborg.h"

enum {
    RTC_ID        = 0,
    RTC_LATCH     = 1,
    RTC_DATA_LOW  = 2,
    RTC_DATA_HIGH = 3
};

typedef struct {
    SysBusDevice busdev;
    int64_t offset;
    int64_t data;
    qemu_irq irq;
} SyborgRTCState;

static uint32_t syborg_rtc_read(void *opaque, target_phys_addr_t offset)
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

static void syborg_rtc_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    SyborgRTCState *s = (SyborgRTCState *)opaque;
    uint64_t now;

    offset &= 0xfff;
    switch (offset >> 2) {
    case RTC_LATCH:
        now = qemu_get_clock(vm_clock);
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

static CPUReadMemoryFunc *syborg_rtc_readfn[] = {
    syborg_rtc_read,
    syborg_rtc_read,
    syborg_rtc_read
};

static CPUWriteMemoryFunc *syborg_rtc_writefn[] = {
    syborg_rtc_write,
    syborg_rtc_write,
    syborg_rtc_write
};

static void syborg_rtc_save(QEMUFile *f, void *opaque)
{
    SyborgRTCState *s = opaque;

    qemu_put_be64(f, s->offset);
    qemu_put_be64(f, s->data);
}

static int syborg_rtc_load(QEMUFile *f, void *opaque, int version_id)
{
    SyborgRTCState *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    s->offset = qemu_get_be64(f);
    s->data = qemu_get_be64(f);

    return 0;
}

static void syborg_rtc_init(SysBusDevice *dev)
{
    SyborgRTCState *s = FROM_SYSBUS(SyborgRTCState, dev);
    struct tm tm;
    int iomemtype;

    iomemtype = cpu_register_io_memory(0, syborg_rtc_readfn,
                                       syborg_rtc_writefn, s);
    sysbus_init_mmio(dev, 0x1000, iomemtype);

    qemu_get_timedate(&tm, 0);
    s->offset = (uint64_t)mktime(&tm) * 1000000000;

    register_savevm("syborg_rtc", -1, 1, syborg_rtc_save, syborg_rtc_load, s);
}

static void syborg_rtc_register_devices(void)
{
    sysbus_register_dev("syborg,rtc", sizeof(SyborgRTCState), syborg_rtc_init);
}

device_init(syborg_rtc_register_devices)
